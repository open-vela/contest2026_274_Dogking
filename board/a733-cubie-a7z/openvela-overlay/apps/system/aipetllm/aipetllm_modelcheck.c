/****************************************************************************
 * apps/system/aipetllm/aipetllm_modelcheck.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <semaphore.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "ggml.h"
#include "aipetllm_sequence.h"
#define GGML_COMMON_DECL_C
#include "ggml-common.h"
#include "ggml-quants.h"
#include "quants.h"

#define GGUF_MAGIC UINT32_C(0x46554747)

static int g_generation_active;
static int g_generation_stop;

int aipetllm_generation_control(int stop)
{
  int active = __atomic_load_n(&g_generation_active, __ATOMIC_ACQUIRE);
  if (stop && active)
    {
      __atomic_store_n(&g_generation_stop, 1, __ATOMIC_RELEASE);
    }

  printf("llm-generation: active=%d stop-request=%d\n", active,
         __atomic_load_n(&g_generation_stop, __ATOMIC_ACQUIRE));
  return 0;
}

static int generation_stopped(void)
{
  return __atomic_load_n(&g_generation_stop, __ATOMIC_ACQUIRE);
}

static double elapsed_seconds(const struct timespec *first,
                              const struct timespec *last)
{
  return (double)(last->tv_sec - first->tv_sec) +
         (double)(last->tv_nsec - first->tv_nsec) / 1.0e9;
}
#define GGUF_TYPE_UINT8 0
#define GGUF_TYPE_INT8 1
#define GGUF_TYPE_UINT16 2
#define GGUF_TYPE_INT16 3
#define GGUF_TYPE_UINT32 4
#define GGUF_TYPE_INT32 5
#define GGUF_TYPE_FLOAT32 6
#define GGUF_TYPE_BOOL 7
#define GGUF_TYPE_STRING 8
#define GGUF_TYPE_ARRAY 9
#define GGUF_TYPE_UINT64 10
#define GGUF_TYPE_INT64 11
#define GGUF_TYPE_FLOAT64 12

#define GGUF_KEY_MAX 256
#define GGUF_NAME_MAX 128
#define GGUF_DIMS_MAX 4
#define GGUF_TENSOR_LIMIT 4096
#define QWEN_LAYER_LIMIT 128

struct tensor_info_s
{
  char name[GGUF_NAME_MAX];
  uint64_t dimensions[GGUF_DIMS_MAX];
  uint64_t offset;
  uint64_t bytes;
  uint32_t dimension_count;
  uint32_t type;
};

struct qwen_layer_s
{
  uint16_t mask;
};

/* One explicit model snapshot shared by commands in the flat image. Keep the
 * lock for a forward operation; concurrent requests fail fast, never free
 * weights that workers are reading. Different models require explicit unload.
 */

static pthread_mutex_t g_model_lock = PTHREAD_MUTEX_INITIALIZER;
static uint8_t *g_model_cache;
static size_t g_model_bytes;
static char g_model_path[1024];
extern int a733_compute_service_start(int (*entry)(int, char **));
static int llm_pool_configure(unsigned int mask);
static void llm_pool_info(void);

int aipetllm_cache_control(int unload)
{
  if (pthread_mutex_trylock(&g_model_lock) != 0)
    {
      fputs("llm-cache: busy; inference/load in progress\n", stderr);
      return 1;
    }

  if (unload)
    {
      if (llm_pool_configure(0) < 0)
        {
          pthread_mutex_unlock(&g_model_lock);
          fputs("llm-cache: pool shutdown failed; model retained\n", stderr);
          return 1;
        }

      free(g_model_cache);
      g_model_cache = NULL;
      g_model_bytes = 0;
      g_model_path[0] = '\0';
      puts("llm-cache: unloaded");
    }
  else
    {
      printf("llm-cache: state=%s bytes=%zu path='%s'\n",
             g_model_cache == NULL ? "empty" : "resident",
             g_model_bytes, g_model_path);
      llm_pool_info();
    }

  pthread_mutex_unlock(&g_model_lock);
  return 0;
}

static int read_exact(FILE *stream, void *buffer, size_t length)
{
  return fread(buffer, 1, length, stream) == length ? 0 : -EIO;
}

static int skip_exact(FILE *stream, uint64_t length)
{
  while (length != 0)
    {
      long step = length > (uint64_t)LONG_MAX ? LONG_MAX : (long)length;
      if (fseek(stream, step, SEEK_CUR) != 0)
        {
          return -errno;
        }

      length -= (uint64_t)step;
    }

  return 0;
}

static int read_string(FILE *stream, char *value, size_t capacity,
                       uint64_t *full_length)
{
  uint64_t length;
  size_t keep;

  if (read_exact(stream, &length, sizeof(length)) < 0)
    {
      return -EIO;
    }

  if (full_length != NULL)
    {
      *full_length = length;
    }

  keep = capacity == 0 ? 0 :
         length < capacity - 1 ? (size_t)length : capacity - 1;
  if (keep != 0 && read_exact(stream, value, keep) < 0)
    {
      return -EIO;
    }

  if (capacity != 0)
    {
      value[keep] = '\0';
    }

  return skip_exact(stream, length - keep);
}

static size_t scalar_size(uint32_t type)
{
  switch (type)
    {
      case GGUF_TYPE_UINT8:
      case GGUF_TYPE_INT8:
      case GGUF_TYPE_BOOL:
        return 1;
      case GGUF_TYPE_UINT16:
      case GGUF_TYPE_INT16:
        return 2;
      case GGUF_TYPE_UINT32:
      case GGUF_TYPE_INT32:
      case GGUF_TYPE_FLOAT32:
        return 4;
      case GGUF_TYPE_UINT64:
      case GGUF_TYPE_INT64:
      case GGUF_TYPE_FLOAT64:
        return 8;
      default:
        return 0;
    }
}

static int skip_value(FILE *stream, uint32_t type, unsigned int depth)
{
  uint32_t element_type;
  uint64_t count;
  uint64_t index;
  uint64_t length;
  size_t size;

  if (depth > 4)
    {
      return -EOVERFLOW;
    }

  size = scalar_size(type);
  if (size != 0)
    {
      return skip_exact(stream, size);
    }

  if (type == GGUF_TYPE_STRING)
    {
      if (read_exact(stream, &length, sizeof(length)) < 0)
        {
          return -EIO;
        }

      return skip_exact(stream, length);
    }

  if (type != GGUF_TYPE_ARRAY ||
      read_exact(stream, &element_type, sizeof(element_type)) < 0 ||
      read_exact(stream, &count, sizeof(count)) < 0)
    {
      return -EINVAL;
    }

  size = scalar_size(element_type);
  if (size != 0)
    {
      if (count > UINT64_MAX / size)
        {
          return -EOVERFLOW;
        }

      return skip_exact(stream, count * size);
    }

  for (index = 0; index < count; index++)
    {
      int result = skip_value(stream, element_type, depth + 1);
      if (result < 0)
        {
          return result;
        }
    }

  return 0;
}

static const char *tensor_type_name(uint32_t type)
{
  switch (type)
    {
      case GGML_TYPE_F32: return "F32";
      case GGML_TYPE_F16: return "F16";
      case GGML_TYPE_Q4_0: return "Q4_0";
      case GGML_TYPE_Q4_1: return "Q4_1";
      case GGML_TYPE_Q5_0: return "Q5_0";
      case GGML_TYPE_Q5_1: return "Q5_1";
      case GGML_TYPE_Q8_0: return "Q8_0";
      case GGML_TYPE_Q8_1: return "Q8_1";
      case GGML_TYPE_Q2_K: return "Q2_K";
      case GGML_TYPE_Q3_K: return "Q3_K";
      case GGML_TYPE_Q4_K: return "Q4_K";
      case GGML_TYPE_Q5_K: return "Q5_K";
      case GGML_TYPE_Q6_K: return "Q6_K";
      case GGML_TYPE_Q8_K: return "Q8_K";
      case GGML_TYPE_I8: return "I8";
      case GGML_TYPE_I16: return "I16";
      case GGML_TYPE_I32: return "I32";
      case GGML_TYPE_I64: return "I64";
      case GGML_TYPE_F64: return "F64";
      case GGML_TYPE_BF16: return "BF16";
      default: return "unsupported";
    }
}

static int tensor_layout(uint32_t type, uint64_t *block, uint64_t *size)
{
  *block = 1;
  switch (type)
    {
      case GGML_TYPE_F32: *size = 4; return 0;
      case GGML_TYPE_F16:
      case GGML_TYPE_BF16: *size = 2; return 0;
      case GGML_TYPE_I8: *size = 1; return 0;
      case GGML_TYPE_I16: *size = 2; return 0;
      case GGML_TYPE_I32: *size = 4; return 0;
      case GGML_TYPE_I64:
      case GGML_TYPE_F64: *size = 8; return 0;
      case GGML_TYPE_Q4_0: *block = QK4_0; *size = sizeof(block_q4_0); return 0;
      case GGML_TYPE_Q4_1: *block = QK4_1; *size = sizeof(block_q4_1); return 0;
      case GGML_TYPE_Q5_0: *block = QK5_0; *size = sizeof(block_q5_0); return 0;
      case GGML_TYPE_Q5_1: *block = QK5_1; *size = sizeof(block_q5_1); return 0;
      case GGML_TYPE_Q8_0: *block = QK8_0; *size = sizeof(block_q8_0); return 0;
      case GGML_TYPE_Q8_1: *block = QK8_1; *size = sizeof(block_q8_1); return 0;
      case GGML_TYPE_Q2_K: *block = QK_K; *size = sizeof(block_q2_K); return 0;
      case GGML_TYPE_Q3_K: *block = QK_K; *size = sizeof(block_q3_K); return 0;
      case GGML_TYPE_Q4_K: *block = QK_K; *size = sizeof(block_q4_K); return 0;
      case GGML_TYPE_Q5_K: *block = QK_K; *size = sizeof(block_q5_K); return 0;
      case GGML_TYPE_Q6_K: *block = QK_K; *size = sizeof(block_q6_K); return 0;
      case GGML_TYPE_Q8_K: *block = QK_K; *size = sizeof(block_q8_K); return 0;
      default: return -ENOTSUP;
    }
}

static int tensor_bytes(const uint64_t *dimensions, uint32_t count,
                        uint32_t type, uint64_t *result)
{
  uint64_t block = 0;
  uint64_t size = 0;
  uint64_t elements = 1;
  uint32_t index;
  int ret;

  ret = tensor_layout(type, &block, &size);
  if (ret < 0 || count == 0 || count > GGUF_DIMS_MAX ||
      dimensions[0] == 0 || dimensions[0] % block != 0)
    {
      return ret < 0 ? ret : -EINVAL;
    }

  for (index = 1; index < count; index++)
    {
      if (dimensions[index] == 0 || elements > UINT64_MAX / dimensions[index])
        {
          return -EOVERFLOW;
        }

      elements *= dimensions[index];
    }

  if (dimensions[0] / block > UINT64_MAX / elements ||
      dimensions[0] / block * elements > UINT64_MAX / size)
    {
      return -EOVERFLOW;
    }

  *result = dimensions[0] / block * elements * size;
  return 0;
}

static uint16_t qwen_tensor_bit(const char *suffix)
{
  static const char *const names[] =
  {
    "attn_norm.weight", "attn_q.weight", "attn_k.weight",
    "attn_v.weight", "attn_output.weight", "ffn_norm.weight",
    "ffn_gate.weight", "ffn_up.weight", "ffn_down.weight"
  };
  unsigned int index;

  for (index = 0; index < sizeof(names) / sizeof(names[0]); index++)
    {
      if (strcmp(suffix, names[index]) == 0)
        {
          return (uint16_t)(1u << index);
        }
    }

  return 0;
}

static void record_qwen_tensor(const char *name, struct qwen_layer_s *layers,
                               int *token_embedding, int *output_norm,
                               int *output)
{
  unsigned int layer;
  int consumed;

  if (strcmp(name, "token_embd.weight") == 0)
    {
      *token_embedding = 1;
    }
  else if (strcmp(name, "output_norm.weight") == 0)
    {
      *output_norm = 1;
    }
  else if (strcmp(name, "output.weight") == 0)
    {
      *output = 1;
    }
  else if (sscanf(name, "blk.%u.%n", &layer, &consumed) == 1 &&
           layer < QWEN_LAYER_LIMIT && consumed > 0)
    {
      layers[layer].mask |= qwen_tensor_bit(name + consumed);
    }
}

int aipetllm_model_checkpoint(const char *path)
{
  struct qwen_layer_s layers[QWEN_LAYER_LIMIT];
  struct tensor_info_s *tensors = NULL;
  struct stat file_info;
  FILE *stream = NULL;
  char architecture[32] = "unknown";
  uint64_t metadata_count;
  uint64_t tensor_count;
  uint64_t file_size;
  uint64_t data_start;
  uint64_t total_bytes = 0;
  uint64_t type_bytes[GGML_TYPE_COUNT];
  uint32_t type_counts[GGML_TYPE_COUNT];
  uint32_t version;
  uint32_t alignment = 32;
  uint32_t block_count = 0;
  uint32_t magic;
  uint64_t index;
  int token_embedding = 0;
  int output_norm = 0;
  int output = 0;
  int ret = 1;

  memset(layers, 0, sizeof(layers));
  memset(type_bytes, 0, sizeof(type_bytes));
  memset(type_counts, 0, sizeof(type_counts));

  if (stat(path, &file_info) < 0 || !S_ISREG(file_info.st_mode))
    {
      fprintf(stderr, "aipetllm: modelcheck cannot stat %s: %d (%s)\n",
              path, errno, strerror(errno));
      return 1;
    }

  file_size = (uint64_t)file_info.st_size;
  stream = fopen(path, "rb");
  if (stream == NULL)
    {
      fprintf(stderr, "aipetllm: modelcheck cannot open %s: %d (%s)\n",
              path, errno, strerror(errno));
      return 1;
    }

  if (read_exact(stream, &magic, sizeof(magic)) < 0 ||
      read_exact(stream, &version, sizeof(version)) < 0 ||
      read_exact(stream, &tensor_count, sizeof(tensor_count)) < 0 ||
      read_exact(stream, &metadata_count, sizeof(metadata_count)) < 0 ||
      magic != GGUF_MAGIC || version < 2 || version > 3 ||
      tensor_count == 0 || tensor_count > GGUF_TENSOR_LIMIT)
    {
      fputs("aipetllm: invalid GGUF header/tensor count\n", stderr);
      goto out;
    }

  for (index = 0; index < metadata_count; index++)
    {
      char key[GGUF_KEY_MAX];
      uint64_t key_length;
      uint32_t type;

      if (read_string(stream, key, sizeof(key), &key_length) < 0 ||
          key_length >= sizeof(key) ||
          read_exact(stream, &type, sizeof(type)) < 0)
        {
          fputs("aipetllm: malformed GGUF metadata\n", stderr);
          goto out;
        }

      if (strcmp(key, "general.architecture") == 0 &&
          type == GGUF_TYPE_STRING)
        {
          if (read_string(stream, architecture, sizeof(architecture), NULL) < 0)
            {
              goto out;
            }
        }
      else if (strcmp(key, "general.alignment") == 0 &&
               type == GGUF_TYPE_UINT32)
        {
          if (read_exact(stream, &alignment, sizeof(alignment)) < 0)
            {
              goto out;
            }
        }
      else if (strstr(key, ".block_count") != NULL &&
               type == GGUF_TYPE_UINT32)
        {
          if (read_exact(stream, &block_count, sizeof(block_count)) < 0)
            {
              goto out;
            }
        }
      else if (skip_value(stream, type, 0) < 0)
        {
          fprintf(stderr, "aipetllm: unsupported metadata key=%s type=%" PRIu32 "\n",
                  key, type);
          goto out;
        }
    }

  if (alignment == 0 || (alignment & (alignment - 1)) != 0 ||
      alignment > 4096 || block_count > QWEN_LAYER_LIMIT)
    {
      fprintf(stderr, "aipetllm: unsafe alignment/layer count %" PRIu32
                      "/%" PRIu32 "\n", alignment, block_count);
      goto out;
    }

  tensors = calloc((size_t)tensor_count, sizeof(*tensors));
  if (tensors == NULL)
    {
      fputs("aipetllm: tensor directory allocation failed\n", stderr);
      goto out;
    }

  for (index = 0; index < tensor_count; index++)
    {
      struct tensor_info_s *tensor = &tensors[index];
      uint64_t name_length;
      uint32_t dimension;

      if (read_string(stream, tensor->name, sizeof(tensor->name),
                      &name_length) < 0 ||
          name_length >= sizeof(tensor->name) ||
          read_exact(stream, &tensor->dimension_count,
                     sizeof(tensor->dimension_count)) < 0 ||
          tensor->dimension_count == 0 ||
          tensor->dimension_count > GGUF_DIMS_MAX)
        {
          fprintf(stderr, "aipetllm: malformed tensor directory at %" PRIu64 "\n",
                  index);
          goto out;
        }

      for (dimension = 0; dimension < tensor->dimension_count; dimension++)
        {
          if (read_exact(stream, &tensor->dimensions[dimension],
                         sizeof(uint64_t)) < 0)
            {
              goto out;
            }
        }

      if (read_exact(stream, &tensor->type, sizeof(tensor->type)) < 0 ||
          read_exact(stream, &tensor->offset, sizeof(tensor->offset)) < 0 ||
          tensor_bytes(tensor->dimensions, tensor->dimension_count,
                       tensor->type, &tensor->bytes) < 0)
        {
          fprintf(stderr, "aipetllm: unsupported/invalid tensor %s type=%" PRIu32
                          " (%s)\n", tensor->name, tensor->type,
                  tensor_type_name(tensor->type));
          goto out;
        }

      if (tensor->type < GGML_TYPE_COUNT)
        {
          type_counts[tensor->type]++;
          type_bytes[tensor->type] += tensor->bytes;
        }

      total_bytes += tensor->bytes;
      record_qwen_tensor(tensor->name, layers, &token_embedding,
                         &output_norm, &output);
    }

  {
    long file_position = ftell(stream);
    if (file_position < 0)
      {
        goto out;
      }

    data_start = ((uint64_t)file_position + alignment - 1) &
                 ~(uint64_t)(alignment - 1);
  }

  for (index = 0; index < tensor_count; index++)
    {
      struct tensor_info_s *tensor = &tensors[index];
      if (tensor->offset % alignment != 0 ||
          tensor->offset > UINT64_MAX - data_start ||
          data_start + tensor->offset > file_size ||
          tensor->bytes > file_size - (data_start + tensor->offset))
        {
          fprintf(stderr, "aipetllm: tensor outside file/alignment name=%s "
                          "offset=%" PRIu64 " bytes=%" PRIu64 "\n",
                  tensor->name, tensor->offset, tensor->bytes);
          goto out;
        }
    }

  printf("model-check path=%s architecture=%s version=%" PRIu32 "\n",
         path, architecture, version);
  printf("file=%" PRIu64 " tensors=%" PRIu64 " metadata=%" PRIu64
         " alignment=%" PRIu32 " data-offset=%" PRIu64 "\n",
         file_size, tensor_count, metadata_count, alignment, data_start);
  printf("tensor-payload=%" PRIu64 " MiB (sum, shared/padding excluded)\n",
         total_bytes / (1024 * 1024));

  for (index = 0; index < GGML_TYPE_COUNT; index++)
    {
      if (type_counts[index] != 0)
        {
          printf("type[%s]=%" PRIu32 " tensors, %" PRIu64 " MiB\n",
                 tensor_type_name((uint32_t)index), type_counts[index],
                 type_bytes[index] / (1024 * 1024));
        }
    }

  if (strcmp(architecture, "qwen2") != 0 || block_count == 0 ||
      !token_embedding || !output_norm)
    {
      fprintf(stderr, "aipetllm: expected qwen2 roots missing "
                      "layers=%" PRIu32 " token=%d norm=%d output=%d\n",
              block_count, token_embedding, output_norm, output);
      goto out;
    }

  for (index = 0; index < block_count; index++)
    {
      if (layers[index].mask != UINT16_C(0x01ff))
        {
          fprintf(stderr, "aipetllm: qwen2 layer %" PRIu64
                          " incomplete mask=%04x expected=01ff\n",
                  index, layers[index].mask);
          goto out;
        }
    }

  printf("qwen2-check passed layers=%" PRIu32
         " roots=token+norm+%s layer-mask=01ff\n",
         block_count, output ? "output" : "tied-output");
  puts("model directory is safe for staged tensor loading; inference is pending.");
  ret = 0;

out:
  free(tensors);
  if (stream != NULL)
    {
      fclose(stream);
    }

  return ret;
}

static uint32_t tensor_crc32(uint32_t crc, const uint8_t *data, size_t length)
{
  size_t index;

  crc = ~crc;
  for (index = 0; index < length; index++)
    {
      unsigned int bit;

      crc ^= data[index];
      for (bit = 0; bit < 8; bit++)
        {
          crc = (crc >> 1) ^ (UINT32_C(0xedb88320) &
                              (uint32_t)-(int32_t)(crc & 1));
        }
    }

  return ~crc;
}

static int read_dequantized_row(FILE *stream, uint64_t absolute_offset,
                                const struct tensor_info_s *tensor,
                                uint64_t row, float *output)
{
  uint64_t row_bytes;
  uint64_t row_dimensions[1];
  void *packed;
  int result = -EINVAL;

  row_dimensions[0] = tensor->dimensions[0];
  if (tensor->dimension_count < 1 ||
      tensor_bytes(row_dimensions, 1, tensor->type, &row_bytes) < 0 ||
      row > UINT64_MAX / row_bytes ||
      absolute_offset > UINT64_MAX - row * row_bytes ||
      absolute_offset + row * row_bytes > (uint64_t)LONG_MAX)
    {
      return -EOVERFLOW;
    }

  if (fseek(stream, (long)(absolute_offset + row * row_bytes), SEEK_SET) != 0)
    {
      return -errno;
    }

  packed = malloc((size_t)row_bytes);
  if (packed == NULL)
    {
      return -ENOMEM;
    }

  if (read_exact(stream, packed, (size_t)row_bytes) < 0)
    {
      result = -EIO;
      goto out;
    }

  switch (tensor->type)
    {
      case GGML_TYPE_F32:
        memcpy(output, packed, (size_t)tensor->dimensions[0] * sizeof(float));
        result = 0;
        break;
      case GGML_TYPE_Q4_K:
        dequantize_row_q4_K((const block_q4_K *)packed, output,
                            (int64_t)tensor->dimensions[0]);
        result = 0;
        break;
      case GGML_TYPE_Q6_K:
        dequantize_row_q6_K((const block_q6_K *)packed, output,
                            (int64_t)tensor->dimensions[0]);
        result = 0;
        break;
      default:
        result = -ENOTSUP;
        break;
    }

out:
  free(packed);
  return result;
}

static int read_quantized_matvec(FILE *stream, uint64_t absolute_offset,
                                 const struct tensor_info_s *tensor,
                                 const float *input, float *output)
{
  uint64_t row_dimensions[1];
  uint64_t row_bytes;
  uint64_t rows;
  void *packed = NULL;
  block_q8_K *input_q8 = NULL;
  uint64_t row;
  int result = -EINVAL;

  if (tensor->dimension_count != 2 || tensor->dimensions[0] == 0 ||
      tensor->dimensions[0] > INT_MAX || tensor->dimensions[1] == 0 ||
      tensor->dimensions[1] > 262144)
    {
      return -EINVAL;
    }

  row_dimensions[0] = tensor->dimensions[0];
  rows = tensor->dimensions[1];
  if (tensor_bytes(row_dimensions, 1, tensor->type, &row_bytes) < 0 ||
      row_bytes == 0 || row_bytes > SIZE_MAX ||
      rows > UINT64_MAX / row_bytes ||
      absolute_offset > UINT64_MAX - rows * row_bytes ||
      absolute_offset > (uint64_t)LONG_MAX)
    {
      return -EOVERFLOW;
    }

  packed = malloc((size_t)row_bytes);
  if (packed == NULL)
    {
      return -ENOMEM;
    }

  if (tensor->type == GGML_TYPE_Q4_K ||
      tensor->type == GGML_TYPE_Q6_K)
    {
      size_t blocks;

      if (tensor->dimensions[0] % QK_K != 0)
        {
          result = -EINVAL;
          goto out;
        }

      blocks = (size_t)(tensor->dimensions[0] / QK_K);
      if (blocks > SIZE_MAX / sizeof(block_q8_K))
        {
          result = -EOVERFLOW;
          goto out;
        }

      input_q8 = malloc(blocks * sizeof(block_q8_K));
      if (input_q8 == NULL)
        {
          result = -ENOMEM;
          goto out;
        }

      quantize_row_q8_K(input, input_q8,
                        (int64_t)tensor->dimensions[0]);
    }
  else if (tensor->type != GGML_TYPE_F32)
    {
      result = -ENOTSUP;
      goto out;
    }

  if (fseek(stream, (long)absolute_offset, SEEK_SET) != 0)
    {
      result = -errno;
      goto out;
    }

  for (row = 0; row < rows; row++)
    {
      if (read_exact(stream, packed, (size_t)row_bytes) < 0)
        {
          result = -EIO;
          goto out;
        }

      if (tensor->type == GGML_TYPE_Q4_K)
        {
          ggml_vec_dot_q4_K_q8_K((int)tensor->dimensions[0],
                                 &output[row], 0, packed, 0,
                                 input_q8, 0, 1);
        }
      else if (tensor->type == GGML_TYPE_Q6_K)
        {
          ggml_vec_dot_q6_K_q8_K((int)tensor->dimensions[0],
                                 &output[row], 0, packed, 0,
                                 input_q8, 0, 1);
        }
      else
        {
          const float *weights = packed;
          double sum = 0.0;
          uint64_t column;

          for (column = 0; column < tensor->dimensions[0]; column++)
            {
              sum += (double)weights[column] * input[column];
            }

          output[row] = (float)sum;
        }
    }

  result = 0;

out:
  free(input_q8);
  free(packed);
  return result;
}

struct matvec_worker_s
{
  const uint8_t *weights;
  const block_q8_K *input;
  float *output;
  size_t row_bytes;
  uint64_t first;
  uint64_t end;
  int columns;
  uint32_t type;
  int cpu;
  int status;
};

static void *matvec_worker(void *argument)
{
  struct matvec_worker_s *worker = argument;
  cpu_set_t affinity = 0;
  uint64_t row;
  int wait;

  CPU_SET(worker->cpu, &affinity);
  worker->status = -EIO;
  if (sched_setaffinity(0, sizeof(affinity), &affinity) < 0)
    {
      return NULL;
    }

  for (wait = 0; wait < 100 && sched_getcpu() != worker->cpu; wait++)
    {
      usleep(1000);
    }

  if (sched_getcpu() != worker->cpu)
    {
      return NULL;
    }

  for (row = worker->first; row < worker->end; row++)
    {
      const void *packed = worker->weights + row * worker->row_bytes;
      if (worker->type == GGML_TYPE_Q4_K)
        {
          ggml_vec_dot_q4_K_q8_K(worker->columns, &worker->output[row],
                               0, packed, 0, worker->input, 0, 1);
        }
      else
        {
          ggml_vec_dot_q6_K_q8_K(worker->columns, &worker->output[row],
                               0, packed, 0, worker->input, 0, 1);
        }
    }

  worker->status = 0;
  return NULL;
}

struct pool_slot_s
{
  struct matvec_worker_s work;
  sem_t ready;
  sem_t done;
  pthread_t thread;
  int stop;
};

static struct pool_slot_s g_pool_slots[8];
static sem_t g_pool_request;
static sem_t g_pool_reply;
static int g_pool_pid;
static int g_pool_count;
static int g_pool_command;
static int g_pool_result;
static unsigned int g_pool_mask;
static unsigned int g_pool_requested_mask;
static uint64_t g_pool_created;
static uint64_t g_pool_jobs;

static void pool_wait(sem_t *semaphore)
{
  while (sem_wait(semaphore) < 0 && errno == EINTR)
    {
    }
}

static void *pool_worker(void *argument)
{
  struct pool_slot_s *slot = argument;
  for (;;)
    {
      pool_wait(&slot->ready);
      if (slot->stop)
        {
          return NULL;
        }

      matvec_worker(&slot->work);
      sem_post(&slot->done);
    }
}

/* All pthread create/join operations belong to this long-lived kernel task's
 * group. Built-in command tasks never attempt cross-group pthread_join.
 * Control storage/semaphores are static: the service remains asleep after
 * unload, while its compute pthreads are stopped and joined before RAM free.
 */

static int llm_pool_service(int argc, char **argv)
{
  int index;
  int cpu;
  (void)argc;
  (void)argv;
  for (;;)
    {
      pool_wait(&g_pool_request);
      g_pool_result = 0;
      if (g_pool_command == 1)
        {
          pthread_attr_t attributes;
          cpu_set_t affinity = g_pool_requested_mask == 0 ?
                               0xff : g_pool_requested_mask;
          for (index = 0; index < g_pool_count; index++)
            {
              g_pool_slots[index].stop = 1;
              sem_post(&g_pool_slots[index].ready);
            }

          for (index = 0; index < g_pool_count; index++)
            {
              if (pthread_join(g_pool_slots[index].thread, NULL) != 0)
                {
                  g_pool_result = -EIO;
                }
            }

          if (g_pool_result < 0)
            {
              sem_post(&g_pool_reply);
              continue;
            }

          g_pool_count = 0;
          g_pool_mask = 0;
          if (sched_setaffinity(0, sizeof(affinity), &affinity) < 0)
            {
              g_pool_result = -EIO;
              sem_post(&g_pool_reply);
              continue;
            }

          pthread_attr_init(&attributes);
          pthread_attr_setstacksize(&attributes, 16384);
          for (cpu = 0; cpu < 8; cpu++)
            {
              struct pool_slot_s *slot;
              if (!(g_pool_requested_mask & (1u << cpu)))
                {
                  continue;
                }

              slot = &g_pool_slots[g_pool_count];
              slot->work.cpu = cpu;
              slot->stop = 0;
              if (pthread_create(&slot->thread, &attributes,
                                 pool_worker, slot) != 0)
                {
                  g_pool_result = -EAGAIN;
                  break;
                }

              g_pool_count++;
              g_pool_created++;
            }

          pthread_attr_destroy(&attributes);
          if (g_pool_result == 0)
            {
              g_pool_mask = g_pool_requested_mask;
            }
        }
      else
        {
          for (index = 0; index < g_pool_count; index++)
            {
              sem_post(&g_pool_slots[index].ready);
            }

          for (index = 0; index < g_pool_count; index++)
            {
              pool_wait(&g_pool_slots[index].done);
              if (g_pool_slots[index].work.status != 0)
                {
                  g_pool_result = g_pool_slots[index].work.status;
                }

              g_pool_slots[index].work.weights = NULL;
              g_pool_slots[index].work.input = NULL;
              g_pool_slots[index].work.output = NULL;
            }

          g_pool_jobs++;
        }

      sem_post(&g_pool_reply);
    }

  return 0;
}

static int llm_pool_configure(unsigned int mask)
{
  int index;
  int initialized = 0;
  if (g_pool_pid == 0)
    {
      if (mask == 0)
        {
          return 0;
        }

      if (sem_init(&g_pool_request, 0, 0) < 0)
        {
          return -ENOMEM;
        }

      if (sem_init(&g_pool_reply, 0, 0) < 0)
        {
          sem_destroy(&g_pool_request);
          return -ENOMEM;
        }
      for (index = 0; index < 8; index++)
        {
          if (sem_init(&g_pool_slots[index].ready, 0, 0) < 0)
            {
              goto initialize_failed;
            }

          if (sem_init(&g_pool_slots[index].done, 0, 0) < 0)
            {
              sem_destroy(&g_pool_slots[index].ready);
              goto initialize_failed;
            }

          initialized++;
        }

      g_pool_pid = a733_compute_service_start(llm_pool_service);
      if (g_pool_pid < 0)
        {
          g_pool_pid = 0;
          goto initialize_failed;
        }
    }

  if (g_pool_mask == mask && (mask == 0 ? g_pool_count == 0 :
                             g_pool_count != 0))
    {
      return 0;
    }

  g_pool_requested_mask = mask;
  g_pool_command = 1;
  sem_post(&g_pool_request);
  pool_wait(&g_pool_reply);
  return g_pool_result;

initialize_failed:
  sem_destroy(&g_pool_request);
  sem_destroy(&g_pool_reply);
  for (index = 0; index < initialized; index++)
    {
      sem_destroy(&g_pool_slots[index].ready);
      sem_destroy(&g_pool_slots[index].done);
    }

  return -EAGAIN;
}

static void llm_pool_info(void)
{
  printf("llm-pool: pid=%d mask=%02x workers=%d created=%" PRIu64
         " matrices=%" PRIu64 "\n", g_pool_pid, g_pool_mask,
         g_pool_count, g_pool_created, g_pool_jobs);
}

/* Only the RAM-backed fast path uses this function. FILE access stays in the
 * caller; workers share immutable packed rows and write disjoint outputs.
 * No global compute state is used, so simultaneous callers cannot mix masks.
 */

static int read_parallel_matvec(FILE *stream, uint64_t absolute_offset,
                               const struct tensor_info_s *tensor,
                               const float *input, float *output,
                               unsigned int cpu_mask,
                               const uint8_t *model_base, size_t model_bytes)
{
  uint64_t dimensions[1];
  uint64_t row_bytes;
  uint64_t rows;
  const uint8_t *weights = NULL;
  uint8_t *copied_weights = NULL;
  block_q8_K *input_q8 = NULL;
  unsigned int total_weight = 0;
  unsigned int cumulative = 0;
  int result = 0;
  int old_cancel;
  int cpu;
  int index;

  if (cpu_mask == 0 || tensor->type == GGML_TYPE_F32)
    {
      return read_quantized_matvec(stream, absolute_offset, tensor,
                                   input, output);
    }

  if (tensor->dimension_count != 2 || tensor->dimensions[0] == 0 ||
      tensor->dimensions[0] > INT_MAX || tensor->dimensions[0] % QK_K != 0 ||
      tensor->dimensions[1] == 0 || tensor->dimensions[1] > 262144 ||
      (tensor->type != GGML_TYPE_Q4_K && tensor->type != GGML_TYPE_Q6_K))
    {
      return -EINVAL;
    }

  dimensions[0] = tensor->dimensions[0];
  rows = tensor->dimensions[1];
  if (tensor_bytes(dimensions, 1, tensor->type, &row_bytes) < 0 ||
      row_bytes == 0 || rows > SIZE_MAX / row_bytes ||
      rows * row_bytes > UINT64_C(268435456) ||
      absolute_offset > (uint64_t)LONG_MAX)
    {
      return -EOVERFLOW;
    }

  if (model_base != NULL)
    {
      if (absolute_offset > model_bytes ||
          rows * row_bytes > model_bytes - absolute_offset ||
          absolute_offset % _Alignof(block_q4_K) != 0)
        {
          return -EOVERFLOW;
        }

      weights = model_base + (size_t)absolute_offset;
    }
  else
    {
      copied_weights = malloc((size_t)(rows * row_bytes));
      weights = copied_weights;
    }
  input_q8 = malloc((size_t)(dimensions[0] / QK_K) * sizeof(block_q8_K));
  if (weights == NULL || input_q8 == NULL)
    {
      result = -ENOMEM;
      goto out;
    }

  if (copied_weights != NULL &&
      (fseek(stream, (long)absolute_offset, SEEK_SET) != 0 ||
       read_exact(stream, copied_weights, (size_t)(rows * row_bytes)) < 0))
    {
      result = -EIO;
      goto out;
    }

  quantize_row_q8_K(input, input_q8, (int64_t)dimensions[0]);
  result = llm_pool_configure(cpu_mask);
  if (result < 0)
    {
      goto out;
    }
  for (cpu = 0; cpu < 8; cpu++)
    {
      if (cpu_mask & (1u << cpu))
        {
          total_weight += cpu >= 6 ? 3 : 1;
        }
    }

  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_cancel);
  for (index = 0; index < g_pool_count; index++)
    {
      struct matvec_worker_s *worker = &g_pool_slots[index].work;
      cpu = worker->cpu;
      worker->weights = weights;
      worker->input = input_q8;
      worker->output = output;
      worker->row_bytes = (size_t)row_bytes;
      worker->columns = (int)dimensions[0];
      worker->type = tensor->type;
      worker->cpu = cpu;
      worker->first = rows * cumulative / total_weight;
      cumulative += cpu >= 6 ? 3 : 1;
      worker->end = rows * cumulative / total_weight;
      worker->status = -EIO;
    }

  g_pool_command = 2;
  sem_post(&g_pool_request);
  pool_wait(&g_pool_reply);
  result = g_pool_result;

  free(input_q8);
  free(copied_weights);
  pthread_setcancelstate(old_cancel, NULL);
  return result;

out:
  free(input_q8);
  free(copied_weights);
  return result;
}

static int add_f32_bias(FILE *stream, uint64_t data_start,
                        const struct tensor_info_s *bias,
                        float *values, uint64_t count)
{
  float *bias_values;
  uint64_t index;
  int result;

  if (bias == NULL || bias->dimension_count != 1 ||
      bias->dimensions[0] != count || bias->type != GGML_TYPE_F32 ||
      data_start > UINT64_MAX - bias->offset)
    {
      return -EINVAL;
    }

  bias_values = malloc((size_t)count * sizeof(float));
  if (bias_values == NULL)
    {
      return -ENOMEM;
    }

  result = read_dequantized_row(stream, data_start + bias->offset,
                                bias, 0, bias_values);
  if (result == 0)
    {
      for (index = 0; index < count; index++)
        {
          values[index] += bias_values[index];
        }
    }

  free(bias_values);
  return result;
}

static int apply_normal_rope(float *values, uint64_t heads,
                             uint64_t head_dimension,
                             uint32_t rotary_dimension,
                             uint32_t position, float frequency_base)
{
  uint64_t head;
  uint32_t pair;

  if (heads == 0 || head_dimension == 0 ||
      heads > UINT64_MAX / head_dimension ||
      rotary_dimension == 0 || (rotary_dimension & 1) != 0 ||
      rotary_dimension > head_dimension || !isfinite(frequency_base) ||
      frequency_base <= 1.0f)
    {
      return -EINVAL;
    }

  for (head = 0; head < heads; head++)
    {
      float *head_values = values + head * head_dimension;

      for (pair = 0; pair < rotary_dimension / 2; pair++)
        {
          uint32_t component = pair * 2;
          float exponent = -(float)component / rotary_dimension;
          float angle = (float)position * powf(frequency_base, exponent);
          float cosine = cosf(angle);
          float sine = sinf(angle);
          float first = head_values[component];
          float second = head_values[component + 1];

          head_values[component] = first * cosine - second * sine;
          head_values[component + 1] = first * sine + second * cosine;
        }
    }

  return 0;
}

/* Qwen2 uses NeoX split-half rotation, not adjacent-pair NORMAL RoPE.
 * Keep the legacy diagnostic helper separate from model execution.
 */

static int apply_qwen2_rope(float *values, uint64_t heads,
                            uint64_t head_dimension,
                            uint32_t rotary_dimension,
                            uint32_t position, float frequency_base)
{
  uint64_t head;
  uint32_t pair;
  if (heads == 0 || head_dimension == 0 ||
      rotary_dimension == 0 || (rotary_dimension & 1) != 0 ||
      rotary_dimension > head_dimension || !isfinite(frequency_base) ||
      frequency_base <= 1.0f)
    {
      return -EINVAL;
    }

  for (head = 0; head < heads; head++)
    {
      float *row = values + head * head_dimension;
      for (pair = 0; pair < rotary_dimension / 2; pair++)
        {
          uint32_t other = pair + rotary_dimension / 2;
          float angle = position *
            powf(frequency_base, -(float)(2 * pair) / rotary_dimension);
          float cosine = cosf(angle);
          float sine = sinf(angle);
          float first = row[pair];
          float second = row[other];
          row[pair] = first * cosine - second * sine;
          row[other] = first * sine + second * cosine;
        }
    }

  return 0;
}

static int normalized_token(FILE *stream, uint64_t data_start,
                            const struct tensor_info_s *embedding,
                            const float *norm_weight, float epsilon,
                            uint32_t token_id, float *input,
                            float *normalized)
{
  uint64_t index;
  double square_sum = 0.0;
  float inverse_rms;

  if (token_id >= embedding->dimensions[1] ||
      data_start > UINT64_MAX - embedding->offset ||
      read_dequantized_row(stream, data_start + embedding->offset,
                           embedding, token_id, input) < 0)
    {
      return -EINVAL;
    }

  for (index = 0; index < embedding->dimensions[0]; index++)
    {
      square_sum += (double)input[index] * input[index];
    }

  inverse_rms = 1.0f /
                sqrtf((float)(square_sum / embedding->dimensions[0]) +
                      epsilon);
  for (index = 0; index < embedding->dimensions[0]; index++)
    {
      normalized[index] = input[index] * inverse_rms * norm_weight[index];
    }

  return 0;
}

static struct tensor_info_s *find_tensor(struct tensor_info_s *tensors,
                                         uint64_t tensor_count,
                                         const char *name)
{
  uint64_t index;

  for (index = 0; index < tensor_count; index++)
    {
      if (strcmp(tensors[index].name, name) == 0)
        {
          return &tensors[index];
        }
    }

  return NULL;
}

static int rmsnorm_vector(FILE *stream, uint64_t data_start,
                          const struct tensor_info_s *norm_tensor,
                          const float *input, uint64_t hidden,
                          float epsilon, float *weight, float *output)
{
  double square_sum = 0.0;
  float inverse_rms;
  uint64_t index;

  if (norm_tensor == NULL || norm_tensor->dimension_count != 1 ||
      norm_tensor->dimensions[0] != hidden ||
      norm_tensor->type != GGML_TYPE_F32 ||
      data_start > UINT64_MAX - norm_tensor->offset ||
      read_dequantized_row(stream, data_start + norm_tensor->offset,
                           norm_tensor, 0, weight) < 0)
    {
      return -EINVAL;
    }

  for (index = 0; index < hidden; index++)
    {
      square_sum += (double)input[index] * input[index];
    }

  inverse_rms = 1.0f /
                sqrtf((float)(square_sum / hidden) + epsilon);
  for (index = 0; index < hidden; index++)
    {
      output[index] = input[index] * inverse_rms * weight[index];
    }

  return 0;
}

static int qwen2_forward1_checkpoint(FILE *stream, uint64_t data_start,
                                     struct tensor_info_s *tensors,
                                     uint64_t tensor_count,
                                     struct tensor_info_s *embedding,
                                     uint32_t token_id,
                                     uint32_t block_count,
                                     uint32_t head_count,
                                     uint32_t head_count_kv,
                                     float epsilon, unsigned int cpu_mask,
                                     const uint8_t *model_base,
                                     size_t model_bytes,
                                     int two_tokens, uint32_t second_token,
                                     uint32_t rotary_dimension,
                                     float frequency_base,
                                     struct aipetllm_sequence_s *sequence)
{
  struct tensor_info_s *output_norm;
  struct tensor_info_s *output_weight;
  float *state = NULL;
  float *normalized = NULL;
  float *norm_weight = NULL;
  float *value = NULL;
  float *context = NULL;
  float *projected = NULL;
  float *gate = NULL;
  float *up = NULL;
  float *swiglu = NULL;
  float *down = NULL;
  float *logits = NULL;
  float *query = NULL;
  float *key = NULL;
  float *keys = NULL;
  float *values = NULL;
  double *scores = NULL;
  float *probability = NULL;
  uint64_t hidden;
  uint64_t ffn_hidden = 0;
  uint64_t head_dimension;
  uint64_t index;
  uint32_t layer;
  uint32_t position;
  uint32_t capacity = two_tokens ? 2 : 1;
  uint32_t current_token = token_id;
  uint32_t next_token = 0;
  struct timespec sequence_start;
  struct timespec first_token_time = {0, 0};
  int streaming = sequence != NULL && sequence->emit != NULL;
  int result = 1;

  clock_gettime(CLOCK_MONOTONIC, &sequence_start);

  if (sequence != NULL)
    {
      if (sequence->input_count == 0 || sequence->input_count > AIPETLLM_INPUT_LIMIT ||
          sequence->generate_limit > 64)
        {
          return 1;
        }

      sequence->output_count = 0;
      capacity = sequence->input_count +
                 (sequence->generate_limit ? sequence->generate_limit - 1 : 0);
      two_tokens = 1;
    }

  if (embedding == NULL || embedding->dimension_count != 2 ||
      token_id >= embedding->dimensions[1] ||
      (two_tokens && second_token >= embedding->dimensions[1]) ||
      block_count == 0 ||
      block_count > QWEN_LAYER_LIMIT || head_count == 0 ||
      head_count_kv == 0 || head_count % head_count_kv != 0)
    {
      fputs("aipetllm: forward1 invalid model metadata\n", stderr);
      return 1;
    }

  if (sequence != NULL)
    {
      for (index = 0; index < sequence->input_count; index++)
        {
          if (sequence->input[index] >= embedding->dimensions[1])
            {
              return 1;
            }
        }
    }

  hidden = embedding->dimensions[0];
  head_dimension = hidden / head_count;
  if (hidden == 0 || hidden > 65536 || hidden % head_count != 0)
    {
      fputs("aipetllm: forward1 invalid hidden/head dimensions\n", stderr);
      return 1;
    }

  state = malloc((size_t)hidden * sizeof(float));
  normalized = malloc((size_t)hidden * sizeof(float));
  norm_weight = malloc((size_t)hidden * sizeof(float));
  context = malloc((size_t)hidden * sizeof(float));
  projected = malloc((size_t)hidden * sizeof(float));
  if (state == NULL || normalized == NULL || norm_weight == NULL ||
      context == NULL || projected == NULL ||
      read_dequantized_row(stream, data_start + embedding->offset,
                           embedding, token_id, state) < 0)
    {
      fputs("aipetllm: forward1 state allocation/load failed\n", stderr);
      goto out;
    }

  printf("forward1 token=%" PRIu32 " layers=%" PRIu32
         " hidden=%" PRIu64 " heads=%" PRIu32 "/%" PRIu32 "\n",
         token_id, block_count, hidden, head_count, head_count_kv);

  if (two_tokens)
    {
      size_t kv_width = (size_t)head_count_kv * head_dimension;
      query = malloc((size_t)hidden * sizeof(float));
      key = malloc(kv_width * sizeof(float));
      keys = calloc((size_t)block_count * capacity * kv_width, sizeof(float));
      values = calloc((size_t)block_count * capacity * kv_width, sizeof(float));
      scores = malloc(capacity * sizeof(double));
      probability = malloc(capacity * sizeof(float));
      if (query == NULL || key == NULL || keys == NULL || values == NULL ||
          scores == NULL || probability == NULL)
        {
          goto out;
        }

      if (rotary_dimension == 0)
        {
          rotary_dimension = (uint32_t)head_dimension;
        }
    }

  for (position = 0; position < capacity; position++)
    {
      current_token = sequence != NULL ?
        (position < sequence->input_count ? sequence->input[position] :
                                           next_token) :
        (position == 0 ? token_id : second_token);
      if (position != 0 &&
          read_dequantized_row(stream, data_start + embedding->offset,
                              embedding, current_token, state) < 0)
        {
          goto out;
        }

      if (two_tokens && (!streaming || position < sequence->input_count))
        {
          printf("forward2 position=%" PRIu32 " token=%" PRIu32 "\n",
                 position, current_token);
        }

  for (layer = 0; layer < block_count; layer++)
    {
      if (sequence != NULL && generation_stopped())
        {
          result = 125;
          goto out;
        }

      struct tensor_info_s *attention_norm;
      struct tensor_info_s *attention_v;
      struct tensor_info_s *attention_v_bias;
      struct tensor_info_s *attention_q;
      struct tensor_info_s *attention_k;
      struct tensor_info_s *attention_q_bias;
      struct tensor_info_s *attention_k_bias;
      struct tensor_info_s *attention_output;
      struct tensor_info_s *ffn_norm;
      struct tensor_info_s *ffn_gate;
      struct tensor_info_s *ffn_up;
      struct tensor_info_s *ffn_down;
      char name[GGUF_NAME_MAX];
      uint64_t value_dimensions;
      uint64_t query_per_kv = head_count / head_count_kv;
      uint32_t attention_changed = 0;
      float attention_max_error = 0.0f;
      uint32_t head;

#define FIND_LAYER_TENSOR(variable, suffix)                                  \
      do                                                                     \
        {                                                                    \
          snprintf(name, sizeof(name), "blk.%" PRIu32 "." suffix, layer);   \
          variable = find_tensor(tensors, tensor_count, name);               \
        }                                                                    \
      while (0)

      FIND_LAYER_TENSOR(attention_norm, "attn_norm.weight");
      FIND_LAYER_TENSOR(attention_v, "attn_v.weight");
      FIND_LAYER_TENSOR(attention_v_bias, "attn_v.bias");
      FIND_LAYER_TENSOR(attention_q, "attn_q.weight");
      FIND_LAYER_TENSOR(attention_k, "attn_k.weight");
      FIND_LAYER_TENSOR(attention_q_bias, "attn_q.bias");
      FIND_LAYER_TENSOR(attention_k_bias, "attn_k.bias");
      FIND_LAYER_TENSOR(attention_output, "attn_output.weight");
      FIND_LAYER_TENSOR(ffn_norm, "ffn_norm.weight");
      FIND_LAYER_TENSOR(ffn_gate, "ffn_gate.weight");
      FIND_LAYER_TENSOR(ffn_up, "ffn_up.weight");
      FIND_LAYER_TENSOR(ffn_down, "ffn_down.weight");
#undef FIND_LAYER_TENSOR

      if (attention_v == NULL || attention_output == NULL ||
          ffn_gate == NULL || ffn_up == NULL || ffn_down == NULL ||
          attention_v->dimension_count != 2 ||
          attention_v->dimensions[0] != hidden ||
          attention_v->dimensions[1] != head_count_kv * head_dimension ||
          attention_output->dimension_count != 2 ||
          attention_output->dimensions[0] != hidden ||
          attention_output->dimensions[1] != hidden ||
          ffn_gate->dimension_count != 2 ||
          ffn_gate->dimensions[0] != hidden ||
          ffn_up->dimension_count != 2 ||
          ffn_up->dimensions[0] != hidden ||
          ffn_up->dimensions[1] != ffn_gate->dimensions[1] ||
          ffn_down->dimension_count != 2 ||
          ffn_down->dimensions[0] != ffn_gate->dimensions[1] ||
          ffn_down->dimensions[1] != hidden)
        {
          fprintf(stderr, "aipetllm: forward1 layer %" PRIu32
                          " tensor layout mismatch\n", layer);
          goto out;
        }

      if (value == NULL)
        {
          ffn_hidden = ffn_gate->dimensions[1];
          value = malloc((size_t)attention_v->dimensions[1] * sizeof(float));
          gate = malloc((size_t)ffn_hidden * sizeof(float));
          up = malloc((size_t)ffn_hidden * sizeof(float));
          swiglu = malloc((size_t)ffn_hidden * sizeof(float));
          down = malloc((size_t)hidden * sizeof(float));
          if (value == NULL || gate == NULL || up == NULL ||
              swiglu == NULL || down == NULL)
            {
              fputs("aipetllm: forward1 work allocation failed\n", stderr);
              goto out;
            }
        }
      else if (ffn_gate->dimensions[1] != ffn_hidden)
        {
          fputs("aipetllm: forward1 inconsistent FFN width\n", stderr);
          goto out;
        }

      value_dimensions = attention_v->dimensions[1];
      if (rmsnorm_vector(stream, data_start, attention_norm, state,
                         hidden, epsilon, norm_weight, normalized) < 0 ||
          read_parallel_matvec(stream, data_start + attention_v->offset,
                                attention_v, normalized, value, cpu_mask,
                                model_base, model_bytes) < 0 ||
          add_f32_bias(stream, data_start, attention_v_bias, value,
                       value_dimensions) < 0)
        {
          fprintf(stderr, "aipetllm: forward1 layer %" PRIu32
                          " attention input failed\n", layer);
          goto out;
        }

      if (two_tokens)
        {
          if (attention_q == NULL || attention_k == NULL ||
              attention_q->dimension_count != 2 ||
              attention_k->dimension_count != 2 ||
              attention_q->dimensions[0] != hidden ||
              attention_q->dimensions[1] != hidden ||
              attention_k->dimensions[0] != hidden ||
              attention_k->dimensions[1] != value_dimensions ||
              read_parallel_matvec(stream, data_start + attention_q->offset,
                                   attention_q, normalized, query, cpu_mask,
                                   model_base, model_bytes) < 0 ||
              read_parallel_matvec(stream, data_start + attention_k->offset,
                                   attention_k, normalized, key, cpu_mask,
                                   model_base, model_bytes) < 0 ||
              add_f32_bias(stream, data_start, attention_q_bias, query,
                           hidden) < 0 ||
              add_f32_bias(stream, data_start, attention_k_bias, key,
                           value_dimensions) < 0 ||
              apply_qwen2_rope(query, head_count, head_dimension,
                                rotary_dimension, position,
                                frequency_base) < 0 ||
              apply_qwen2_rope(key, head_count_kv, head_dimension,
                                rotary_dimension, position,
                                frequency_base) < 0)
            {
              fputs("aipetllm: forward2 Q/K/RoPE failed\n", stderr);
              goto out;
            }

          memcpy(keys + ((size_t)layer * capacity + position) * value_dimensions,
                 key, (size_t)value_dimensions * sizeof(float));
          memcpy(values + ((size_t)layer * capacity + position) * value_dimensions,
                 value, (size_t)value_dimensions * sizeof(float));
        }

      for (head = 0; head < head_count; head++)
        {
          uint64_t source = (uint64_t)(head / query_per_kv) *
                            head_dimension;
          uint64_t destination = (uint64_t)head * head_dimension;

          if (!two_tokens || position == 0)
            {
              memcpy(&context[destination], &value[source],
                     (size_t)head_dimension * sizeof(float));
            }
          else
            {
              float maximum;
              float total;
              uint32_t past;
              uint64_t component;
              for (past = 0; past <= position; past++)
                {
                  size_t base = ((size_t)layer * capacity + past) *
                                value_dimensions + source;
                  scores[past] = 0.0;
                  for (component = 0; component < head_dimension; component++)
                    {
                      scores[past] += (double)query[destination + component] *
                                      keys[base + component];
                    }

                  scores[past] *= 1.0 / sqrt((double)head_dimension);
                }

              maximum = (float)scores[0];
              for (past = 1; past <= position; past++)
                {
                  maximum = fmaxf(maximum, (float)scores[past]);
                }

              total = 0.0f;
              for (past = 0; past <= position; past++)
                {
                  probability[past] = expf((float)scores[past] - maximum);
                  total += probability[past];
                }

              if (!isfinite(total) || total <= 0.0f)
                {
                  goto out;
                }
              for (component = 0; component < head_dimension; component++)
                {
                  size_t base = (size_t)layer * capacity * value_dimensions +
                                source + component;
                  float sum = 0.0f;
                  for (past = 0; past <= position; past++)
                    {
                      sum += probability[past] / total *
                             values[base + (size_t)past * value_dimensions];
                    }

                  context[destination + component] = sum;
                  if (position == 1)
                    {
                      /* Retain the v93 two-key expression as a compatibility
                       * baseline and measure the generic reduction before
                       * selecting it. This does not prove model accuracy.
                       */
                      float baseline =
                        probability[0] / total * values[base] +
                        probability[1] / total * values[base + value_dimensions];
                      if (memcmp(&sum, &baseline, sizeof(float)) != 0)
                        {
                          attention_changed++;
                          attention_max_error = fmaxf(attention_max_error,
                                                       fabsf(sum - baseline));
                        }

                      context[destination + component] = baseline;
                    }
                }
            }
        }

      if (two_tokens && position == 1 && !streaming)
        {
          printf("attention-audit layer=%" PRIu32
                 " generic-vs-two-key changed=%" PRIu32 " maxabs=%.9g"
                 " q=%08" PRIx32 " k=%08" PRIx32 " v=%08" PRIx32
                 " context=%08" PRIx32 "; two-key selected\n",
                 layer, attention_changed, (double)attention_max_error,
                 tensor_crc32(0, (const uint8_t *)query, hidden * sizeof(float)),
                 tensor_crc32(0, (const uint8_t *)key,
                              value_dimensions * sizeof(float)),
                 tensor_crc32(0, (const uint8_t *)value,
                              value_dimensions * sizeof(float)),
                 tensor_crc32(0, (const uint8_t *)context, hidden * sizeof(float)));
        }

      if (read_parallel_matvec(stream,
                                data_start + attention_output->offset,
                                attention_output, context, projected,
                                cpu_mask, model_base, model_bytes) < 0)
        {
          fprintf(stderr, "aipetllm: forward1 layer %" PRIu32
                          " attention output failed\n", layer);
          goto out;
        }

      for (index = 0; index < hidden; index++)
        {
          state[index] += projected[index];
        }

      if (rmsnorm_vector(stream, data_start, ffn_norm, state, hidden,
                         epsilon, norm_weight, normalized) < 0 ||
          read_parallel_matvec(stream, data_start + ffn_gate->offset,
                                ffn_gate, normalized, gate, cpu_mask,
                                model_base, model_bytes) < 0 ||
          read_parallel_matvec(stream, data_start + ffn_up->offset,
                                ffn_up, normalized, up, cpu_mask,
                                model_base, model_bytes) < 0)
        {
          fprintf(stderr, "aipetllm: forward1 layer %" PRIu32
                          " FFN input failed\n", layer);
          goto out;
        }

      for (index = 0; index < ffn_hidden; index++)
        {
          float exponential;
          float silu;

          if (gate[index] >= 0.0f)
            {
              silu = gate[index] / (1.0f + expf(-gate[index]));
            }
          else
            {
              exponential = expf(gate[index]);
              silu = gate[index] * exponential / (1.0f + exponential);
            }

          swiglu[index] = silu * up[index];
        }

      if (read_parallel_matvec(stream, data_start + ffn_down->offset,
                                ffn_down, swiglu, down, cpu_mask,
                                model_base, model_bytes) < 0)
        {
          fprintf(stderr, "aipetllm: forward1 layer %" PRIu32
                          " FFN down failed\n", layer);
          goto out;
        }

      for (index = 0; index < hidden; index++)
        {
          state[index] += down[index];
        }

      if (!streaming && (sequence == NULL || !sequence->chat_mode ||
          layer == 0 || layer + 1 == block_count))
        {
      printf("layer[%" PRIu32 "] state-crc=%08" PRIx32 "\n",
             layer,
             tensor_crc32(0, (const uint8_t *)state,
                          (size_t)hidden * sizeof(float)));
        }
    }

  if (sequence != NULL && sequence->chat_mode &&
      position + 1 < sequence->input_count)
    {
      /* History KV is complete; only the final prompt position needs logits. */
      continue;
    }

  output_norm = find_tensor(tensors, tensor_count, "output_norm.weight");
  output_weight = find_tensor(tensors, tensor_count, "output.weight");
  if (output_weight == NULL)
    {
      output_weight = embedding;
    }

  if (rmsnorm_vector(stream, data_start, output_norm, state, hidden,
                     epsilon, norm_weight, normalized) < 0 ||
      output_weight->dimension_count != 2 ||
      output_weight->dimensions[0] != hidden ||
      output_weight->dimensions[1] == 0 ||
      output_weight->dimensions[1] > 262144)
    {
      fputs("aipetllm: forward1 output tensor mismatch\n", stderr);
      goto out;
    }

  if (logits == NULL)
    {
      logits = malloc((size_t)output_weight->dimensions[1] * sizeof(float));
    }
  if (logits == NULL ||
      read_parallel_matvec(stream, data_start + output_weight->offset,
                            output_weight, normalized, logits, cpu_mask,
                            model_base, model_bytes) < 0)
    {
      fputs("aipetllm: forward1 LM head failed\n", stderr);
      goto out;
    }

  {
    uint64_t maximum_index = 0;
    float maximum_logit = logits[0];

    for (index = 1; index < output_weight->dimensions[1]; index++)
      {
        if (!isfinite(logits[index]))
          {
            goto out;
          }
        if (logits[index] > maximum_logit)
          {
            maximum_logit = logits[index];
            maximum_index = index;
          }
      }

    if (!streaming)
      {
    printf("final-state-crc=%08" PRIx32
           " logits-crc=%08" PRIx32
           " argmax=%" PRIu64 " logit=%.9g vocab=%" PRIu64 "\n",
           tensor_crc32(0, (const uint8_t *)normalized,
                        (size_t)hidden * sizeof(float)),
           tensor_crc32(0, (const uint8_t *)logits,
                        (size_t)output_weight->dimensions[1] *
                        sizeof(float)),
           maximum_index, (double)maximum_logit,
           output_weight->dimensions[1]);
      }
    if (!isfinite(maximum_logit))
      {
        goto out;
      }

    next_token = (uint32_t)maximum_index;
    if (sequence != NULL && sequence->generate_limit != 0 &&
        position + 1 >= sequence->input_count)
      {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (sequence->output_count == 0)
          {
            first_token_time = now;
            sequence->first_token_seconds = elapsed_seconds(&sequence_start,
                                                             &now);
            if (streaming) fputs("\nassistant> ", stdout);
          }
        sequence->after_first_seconds = elapsed_seconds(&first_token_time,
                                                        &now);
        sequence->output[sequence->output_count++] = next_token;
        if (!streaming)
          {
        printf("generated[%" PRIu32 "]=%" PRIu32 "\n",
               sequence->output_count - 1, next_token);
          }
        else if (next_token != sequence->eos &&
                 (!sequence->chat_mode || next_token != sequence->chat_stop) &&
                 sequence->emit(sequence->emit_context, next_token) != 0)
          {
            goto out;
          }
      }
  }

  if (sequence != NULL && sequence->output_count != 0 &&
      (next_token == sequence->eos ||
       (sequence->chat_mode && next_token == sequence->chat_stop)))
    {
      /* Do not display control markers as assistant response text. */
      if (sequence->chat_mode)
        {
          sequence->output_count--;
        }
      if (!streaming) printf("generation-stop token=%" PRIu32 "\n", next_token);
      break;
    }

    }

  if (sequence != NULL)
    {
      if (streaming) putchar('\n');
      printf("Qwen2 causal prefill/generation passed; prompt=%" PRIu32
             " generated=%" PRIu32 "; KV session released on return\n",
             sequence->input_count, sequence->output_count);
    }
  else if (two_tokens)
    {
      puts("Qwen2 28-layer two-token causal KV checkpoint passed; "
           "session persistence and generation pending.");
    }
  else
    {
  puts("Qwen2 28-layer single-token forward and LM head checkpoint passed; "
       "multi-token KV-cache generation pending.");
    }
  result = 0;

out:
  if (result == 125)
    {
      puts("\ngeneration stopped cooperatively; no partial history commit");
    }
  free(probability);
  free(scores);
  free(values);
  free(keys);
  free(key);
  free(query);
  free(logits);
  free(down);
  free(swiglu);
  free(up);
  free(gate);
  free(projected);
  free(context);
  free(value);
  free(norm_weight);
  free(normalized);
  free(state);
  return result;
}

static int embedding_pipeline_run(const char *path,
                                         uint32_t token_id,
                                         int projection_mode,
                                         uint32_t position,
                                         uint32_t second_token_id,
                                         struct aipetllm_sequence_s *sequence)
{
  unsigned int cpu_mask = (unsigned int)projection_mode >> 8;
  struct tensor_info_s *embedding = NULL;
  struct tensor_info_s *attention_norm = NULL;
  struct tensor_info_s *attention_q = NULL;
  struct tensor_info_s *attention_k = NULL;
  struct tensor_info_s *attention_v = NULL;
  struct tensor_info_s *attention_q_bias = NULL;
  struct tensor_info_s *attention_k_bias = NULL;
  struct tensor_info_s *attention_v_bias = NULL;
  struct tensor_info_s *attention_output_weight = NULL;
  struct tensor_info_s *ffn_norm = NULL;
  struct tensor_info_s *ffn_gate = NULL;
  struct tensor_info_s *ffn_up = NULL;
  struct tensor_info_s *ffn_down = NULL;
  struct tensor_info_s *tensors = NULL;
  FILE *stream = NULL;
  void *model_memory = NULL;
  size_t model_length = 0;
  int cache_locked = 0;
  int cache_hit = 0;
  int old_cancel = PTHREAD_CANCEL_ENABLE;
  struct timespec load_start;
  struct timespec compute_start;
  struct timespec finished;
  uint64_t metadata_count;
  uint64_t tensor_count;
  uint64_t data_start;
  uint32_t alignment = 32;
  uint32_t version;
  uint32_t magic;
  uint32_t head_count = 0;
  uint32_t head_count_kv = 0;
  uint32_t rotary_dimension = 0;
  uint32_t block_count = 0;
  float epsilon = 1.0e-6f;
  float frequency_base = 1000000.0f;
  float *input = NULL;
  float *weight = NULL;
  float *normalized = NULL;
  float *q_output = NULL;
  float *k_output = NULL;
  float *v_output = NULL;
  float *second_input = NULL;
  float *second_normalized = NULL;
  float *second_q = NULL;
  float *second_k = NULL;
  float *second_v = NULL;
  float *attention_output = NULL;
  float *projected_output = NULL;
  float *residual_output = NULL;
  float *post_attention = NULL;
  float *ffn_norm_weight = NULL;
  float *ffn_gate_output = NULL;
  float *ffn_up_output = NULL;
  float *ffn_swiglu = NULL;
  float *ffn_down_output = NULL;
  float *block_output = NULL;
  uint64_t index;
  double square_sum = 0.0;
  double output_sum = 0.0;
  double output_square_sum = 0.0;
  float inverse_rms;
  float minimum = 0.0f;
  float maximum = 0.0f;
  int ret = 1;

  projection_mode &= 255;
  if (projection_mode >= 7)
    {
      if (strlen(path) >= sizeof(g_model_path) ||
          pthread_mutex_trylock(&g_model_lock) != 0)
        {
          fputs("forwardfast: cache busy or model path too long\n", stderr);
          return 1;
        }

      cache_locked = 1;
      pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_cancel);
      if (sequence != NULL)
        {
          __atomic_store_n(&g_generation_stop, 0, __ATOMIC_RELEASE);
          __atomic_store_n(&g_generation_active, 1, __ATOMIC_RELEASE);
        }
      if (g_model_cache != NULL)
        {
          if (strcmp(path, g_model_path) != 0)
            {
              fputs("forwardfast: unload existing model before switching\n",
                    stderr);
              goto out;
            }

          cache_hit = 1;
          model_memory = g_model_cache;
          model_length = g_model_bytes;
          clock_gettime(CLOCK_MONOTONIC, &compute_start);
          stream = fmemopen(model_memory, model_length, "rb");
          puts("forwardfast: cache-hit; load=0 sec; no model SD reads");
        }
    }

  if (!cache_hit)
    {
      stream = fopen(path, "rb");
    }
  if (stream == NULL)
    {
      fprintf(stderr, "aipetllm: embed cannot open %s: %d (%s)\n",
              path, errno, strerror(errno));
      goto out;
    }

  if (projection_mode >= 7 && !cache_hit)
    {
      struct stat model_stat;
      size_t loaded = 0;
      size_t length;
      FILE *memory_stream;

      if (stat(path, &model_stat) < 0 || !S_ISREG(model_stat.st_mode) ||
          model_stat.st_size < 24 ||
          (uint64_t)model_stat.st_size > UINT64_C(1610612736))
        {
          fputs("forwardfast: model must be a readable file <= 1536 MiB\n",
                stderr);
          goto out;
        }

      length = (size_t)model_stat.st_size;
      model_length = length;
      clock_gettime(CLOCK_MONOTONIC, &load_start);
      model_memory = malloc(length);
      if (model_memory == NULL)
        {
          fputs("forwardfast: model RAM allocation failed\n", stderr);
          goto out;
        }

      printf("forwardfast: loading %zu bytes into RAM\n", length);
      while (loaded < length)
        {
          if (sequence != NULL && generation_stopped())
            {
              ret = 125;
              goto out;
            }
          size_t chunk = length - loaded;
          if (chunk > 262144)
            {
              chunk = 262144;
            }

          if (read_exact(stream, (uint8_t *)model_memory + loaded, chunk) < 0)
            {
              fputs("forwardfast: short model read\n", stderr);
              goto out;
            }

          loaded += chunk;
        }

      memory_stream = fmemopen(model_memory, length, "rb");
      if (memory_stream == NULL)
        {
          fputs("forwardfast: memory stream failed\n", stderr);
          goto out;
        }

      fclose(stream);
      stream = memory_stream;
      clock_gettime(CLOCK_MONOTONIC, &compute_start);
      printf("forwardfast: load=%.4f sec; model reads now RAM-only\n",
             (double)(compute_start.tv_sec - load_start.tv_sec) +
             (double)(compute_start.tv_nsec - load_start.tv_nsec) / 1.0e9);
    }

  if (read_exact(stream, &magic, sizeof(magic)) < 0 ||
      read_exact(stream, &version, sizeof(version)) < 0 ||
      read_exact(stream, &tensor_count, sizeof(tensor_count)) < 0 ||
      read_exact(stream, &metadata_count, sizeof(metadata_count)) < 0 ||
      magic != GGUF_MAGIC || version < 2 || version > 3 ||
      tensor_count == 0 || tensor_count > GGUF_TENSOR_LIMIT)
    {
      fputs("aipetllm: embed invalid GGUF header\n", stderr);
      goto out;
    }

  for (index = 0; index < metadata_count; index++)
    {
      char key[GGUF_KEY_MAX];
      uint64_t key_length;
      uint32_t type;

      if (read_string(stream, key, sizeof(key), &key_length) < 0 ||
          key_length >= sizeof(key) ||
          read_exact(stream, &type, sizeof(type)) < 0)
        {
          fputs("aipetllm: embed malformed metadata\n", stderr);
          goto out;
        }

      if (strcmp(key, "general.alignment") == 0 &&
          type == GGUF_TYPE_UINT32)
        {
          if (read_exact(stream, &alignment, sizeof(alignment)) < 0)
            {
              goto out;
            }
        }
      else if (sequence != NULL &&
               strcmp(key, "tokenizer.ggml.eos_token_id") == 0 &&
               type == GGUF_TYPE_UINT32)
        {
          if (read_exact(stream, &sequence->eos, sizeof(sequence->eos)) < 0)
            {
              goto out;
            }
        }
      else if (strcmp(key,
                      "qwen2.attention.layer_norm_rms_epsilon") == 0 &&
               type == GGUF_TYPE_FLOAT32)
        {
          if (read_exact(stream, &epsilon, sizeof(epsilon)) < 0)
            {
              goto out;
            }
        }
      else if (strcmp(key, "qwen2.attention.head_count") == 0 &&
               type == GGUF_TYPE_UINT32)
        {
          if (read_exact(stream, &head_count, sizeof(head_count)) < 0)
            {
              goto out;
            }
        }
      else if (strcmp(key, "qwen2.attention.head_count_kv") == 0 &&
               type == GGUF_TYPE_UINT32)
        {
          if (read_exact(stream, &head_count_kv, sizeof(head_count_kv)) < 0)
            {
              goto out;
            }
        }
      else if (strstr(key, ".block_count") != NULL &&
               type == GGUF_TYPE_UINT32)
        {
          if (read_exact(stream, &block_count, sizeof(block_count)) < 0)
            {
              goto out;
            }
        }
      else if (strcmp(key, "qwen2.rope.dimension_count") == 0 &&
               type == GGUF_TYPE_UINT32)
        {
          if (read_exact(stream, &rotary_dimension,
                         sizeof(rotary_dimension)) < 0)
            {
              goto out;
            }
        }
      else if (strcmp(key, "qwen2.rope.freq_base") == 0 &&
               type == GGUF_TYPE_FLOAT32)
        {
          if (read_exact(stream, &frequency_base,
                         sizeof(frequency_base)) < 0)
            {
              goto out;
            }
        }
      else if (skip_value(stream, type, 0) < 0)
        {
          goto out;
        }
    }

  if (alignment == 0 || (alignment & (alignment - 1)) != 0 ||
      alignment > 4096 || !isfinite(epsilon) ||
      epsilon <= 0.0f || epsilon > 0.01f)
    {
      fprintf(stderr, "aipetllm: embed unsafe alignment/epsilon %" PRIu32
                      "/%.9g\n", alignment, (double)epsilon);
      goto out;
    }

  tensors = calloc((size_t)tensor_count, sizeof(*tensors));
  if (tensors == NULL)
    {
      fputs("aipetllm: embed tensor directory allocation failed\n", stderr);
      goto out;
    }

  for (index = 0; index < tensor_count; index++)
    {
      struct tensor_info_s *tensor = &tensors[index];
      uint64_t name_length;
      uint32_t dimension;

      if (read_string(stream, tensor->name, sizeof(tensor->name),
                      &name_length) < 0 ||
          name_length >= sizeof(tensor->name) ||
          read_exact(stream, &tensor->dimension_count,
                     sizeof(tensor->dimension_count)) < 0 ||
          tensor->dimension_count == 0 ||
          tensor->dimension_count > GGUF_DIMS_MAX)
        {
          goto out;
        }

      for (dimension = 0; dimension < tensor->dimension_count; dimension++)
        {
          if (read_exact(stream, &tensor->dimensions[dimension],
                         sizeof(uint64_t)) < 0)
            {
              goto out;
            }
        }

      if (read_exact(stream, &tensor->type, sizeof(tensor->type)) < 0 ||
          read_exact(stream, &tensor->offset, sizeof(tensor->offset)) < 0 ||
          tensor_bytes(tensor->dimensions, tensor->dimension_count,
                       tensor->type, &tensor->bytes) < 0)
        {
          goto out;
        }

      if (strcmp(tensor->name, "token_embd.weight") == 0)
        {
          embedding = tensor;
        }
      else if (strcmp(tensor->name, "blk.0.attn_norm.weight") == 0)
        {
          attention_norm = tensor;
        }
      else if (strcmp(tensor->name, "blk.0.attn_q.weight") == 0)
        {
          attention_q = tensor;
        }
      else if (strcmp(tensor->name, "blk.0.attn_k.weight") == 0)
        {
          attention_k = tensor;
        }
      else if (strcmp(tensor->name, "blk.0.attn_v.weight") == 0)
        {
          attention_v = tensor;
        }
      else if (strcmp(tensor->name, "blk.0.attn_q.bias") == 0)
        {
          attention_q_bias = tensor;
        }
      else if (strcmp(tensor->name, "blk.0.attn_k.bias") == 0)
        {
          attention_k_bias = tensor;
        }
      else if (strcmp(tensor->name, "blk.0.attn_v.bias") == 0)
        {
          attention_v_bias = tensor;
        }
      else if (strcmp(tensor->name, "blk.0.attn_output.weight") == 0)
        {
          attention_output_weight = tensor;
        }
      else if (strcmp(tensor->name, "blk.0.ffn_norm.weight") == 0)
        {
          ffn_norm = tensor;
        }
      else if (strcmp(tensor->name, "blk.0.ffn_gate.weight") == 0)
        {
          ffn_gate = tensor;
        }
      else if (strcmp(tensor->name, "blk.0.ffn_up.weight") == 0)
        {
          ffn_up = tensor;
        }
      else if (strcmp(tensor->name, "blk.0.ffn_down.weight") == 0)
        {
          ffn_down = tensor;
        }
    }

  {
    long directory_end = ftell(stream);
    if (directory_end < 0)
      {
        goto out;
      }

    data_start = ((uint64_t)directory_end + alignment - 1) &
                 ~(uint64_t)(alignment - 1);
  }

  if (projection_mode >= 6)
    {
      ret = qwen2_forward1_checkpoint(stream, data_start, tensors,
                                      tensor_count, embedding, token_id,
                                      block_count, head_count,
                                      head_count_kv, epsilon, cpu_mask,
                                      model_memory, model_length,
                                      projection_mode == 8, second_token_id,
                                      rotary_dimension, frequency_base,
                                      sequence);
      goto out;
    }

  if (embedding == NULL || attention_norm == NULL ||
      embedding->dimension_count != 2 ||
      attention_norm->dimension_count != 1 ||
      embedding->dimensions[0] == 0 ||
      embedding->dimensions[0] != attention_norm->dimensions[0] ||
      embedding->dimensions[0] > 65536 ||
      token_id >= embedding->dimensions[1] ||
      attention_norm->type != GGML_TYPE_F32 ||
      data_start > (uint64_t)LONG_MAX)
    {
      fputs("aipetllm: embed incompatible embedding/norm tensors\n", stderr);
      goto out;
    }

  if (projection_mode >= 1 &&
      (attention_q == NULL || attention_q->dimension_count != 2 ||
       attention_q->dimensions[0] != embedding->dimensions[0] ||
       attention_q->dimensions[1] == 0 ||
       attention_q->dimensions[1] > 65536 ||
       (attention_q->type != GGML_TYPE_F32 &&
        attention_q->type != GGML_TYPE_Q4_K &&
        attention_q->type != GGML_TYPE_Q6_K) ||
       data_start > UINT64_MAX - attention_q->offset))
    {
      fputs("aipetllm: qproj incompatible blk.0.attn_q.weight\n", stderr);
      goto out;
    }

  if (projection_mode >= 2 &&
      (attention_k == NULL || attention_v == NULL ||
       attention_k->dimension_count != 2 ||
       attention_v->dimension_count != 2 ||
       attention_k->dimensions[0] != embedding->dimensions[0] ||
       attention_v->dimensions[0] != embedding->dimensions[0] ||
       attention_k->dimensions[1] == 0 ||
       attention_k->dimensions[1] != attention_v->dimensions[1] ||
       attention_k->dimensions[1] > 65536 ||
       (attention_k->type != GGML_TYPE_F32 &&
        attention_k->type != GGML_TYPE_Q4_K &&
        attention_k->type != GGML_TYPE_Q6_K) ||
       (attention_v->type != GGML_TYPE_F32 &&
        attention_v->type != GGML_TYPE_Q4_K &&
        attention_v->type != GGML_TYPE_Q6_K) ||
       data_start > UINT64_MAX - attention_k->offset ||
       data_start > UINT64_MAX - attention_v->offset ||
       head_count == 0 || head_count_kv == 0 ||
       attention_q->dimensions[1] % head_count != 0 ||
       attention_k->dimensions[1] % head_count_kv != 0 ||
       attention_q->dimensions[1] / head_count !=
       attention_k->dimensions[1] / head_count_kv))
    {
      fputs("aipetllm: qkv incompatible K/V/GQA metadata\n", stderr);
      goto out;
    }

  if (projection_mode >= 2 && rotary_dimension == 0)
    {
      rotary_dimension = (uint32_t)(attention_q->dimensions[1] /
                                    head_count);
    }

  if (projection_mode >= 4 &&
      (attention_output_weight == NULL || ffn_norm == NULL ||
       attention_output_weight->dimension_count != 2 ||
       attention_output_weight->dimensions[0] !=
       attention_q->dimensions[1] ||
       attention_output_weight->dimensions[1] !=
       embedding->dimensions[0] ||
       (attention_output_weight->type != GGML_TYPE_F32 &&
        attention_output_weight->type != GGML_TYPE_Q4_K &&
        attention_output_weight->type != GGML_TYPE_Q6_K) ||
       ffn_norm->dimension_count != 1 ||
       ffn_norm->dimensions[0] != embedding->dimensions[0] ||
       ffn_norm->type != GGML_TYPE_F32 ||
       data_start > UINT64_MAX - attention_output_weight->offset ||
       data_start > UINT64_MAX - ffn_norm->offset))
    {
      fputs("aipetllm: attnblock2 incompatible output/norm tensors\n",
            stderr);
      goto out;
    }

  if (projection_mode >= 5 &&
      (ffn_gate == NULL || ffn_up == NULL || ffn_down == NULL ||
       ffn_gate->dimension_count != 2 ||
       ffn_up->dimension_count != 2 ||
       ffn_down->dimension_count != 2 ||
       ffn_gate->dimensions[0] != embedding->dimensions[0] ||
       ffn_up->dimensions[0] != embedding->dimensions[0] ||
       ffn_gate->dimensions[1] == 0 ||
       ffn_gate->dimensions[1] != ffn_up->dimensions[1] ||
       ffn_down->dimensions[0] != ffn_gate->dimensions[1] ||
       ffn_down->dimensions[1] != embedding->dimensions[0] ||
       ffn_gate->dimensions[1] > 65536 ||
       (ffn_gate->type != GGML_TYPE_F32 &&
        ffn_gate->type != GGML_TYPE_Q4_K &&
        ffn_gate->type != GGML_TYPE_Q6_K) ||
       (ffn_up->type != GGML_TYPE_F32 &&
        ffn_up->type != GGML_TYPE_Q4_K &&
        ffn_up->type != GGML_TYPE_Q6_K) ||
       (ffn_down->type != GGML_TYPE_F32 &&
        ffn_down->type != GGML_TYPE_Q4_K &&
        ffn_down->type != GGML_TYPE_Q6_K) ||
       data_start > UINT64_MAX - ffn_gate->offset ||
       data_start > UINT64_MAX - ffn_up->offset ||
       data_start > UINT64_MAX - ffn_down->offset))
    {
      fputs("aipetllm: block2 incompatible SwiGLU tensors\n", stderr);
      goto out;
    }

  input = malloc((size_t)embedding->dimensions[0] * sizeof(float));
  weight = malloc((size_t)embedding->dimensions[0] * sizeof(float));
  normalized = malloc((size_t)embedding->dimensions[0] * sizeof(float));
  if (input == NULL || weight == NULL || normalized == NULL)
    {
      fputs("aipetllm: embed vector allocation failed\n", stderr);
      goto out;
    }

  if (read_dequantized_row(stream, data_start + embedding->offset,
                           embedding, token_id, input) < 0 ||
      read_dequantized_row(stream, data_start + attention_norm->offset,
                           attention_norm, 0, weight) < 0)
    {
      fputs("aipetllm: embed tensor row read/dequantize failed\n", stderr);
      goto out;
    }

  for (index = 0; index < embedding->dimensions[0]; index++)
    {
      square_sum += (double)input[index] * input[index];
    }

  inverse_rms = 1.0f /
                sqrtf((float)(square_sum / embedding->dimensions[0]) +
                      epsilon);
  for (index = 0; index < embedding->dimensions[0]; index++)
    {
      float value = input[index] * inverse_rms * weight[index];

      normalized[index] = value;
      output_sum += value;
      output_square_sum += (double)value * value;
      if (index == 0 || value < minimum)
        {
          minimum = value;
        }

      if (index == 0 || value > maximum)
        {
          maximum = value;
        }
    }

  printf("embedding token=%" PRIu32 " hidden=%" PRIu64
         " vocab=%" PRIu64 " type=%s row-bytes=%" PRIu64 "\n",
         token_id, embedding->dimensions[0], embedding->dimensions[1],
         tensor_type_name(embedding->type),
         embedding->bytes / embedding->dimensions[1]);
  printf("rmsnorm tensor=blk.0.attn_norm.weight epsilon=%.9g "
         "input-rms=%.9g inverse-rms=%.9g\n",
         (double)epsilon,
         sqrt(square_sum / embedding->dimensions[0]),
         (double)inverse_rms);
  printf("embedding-crc=%08" PRIx32 " normalized-crc=%08" PRIx32
         " sum=%.9g l2=%.9g min=%.9g max=%.9g\n",
         tensor_crc32(0, (const uint8_t *)input,
                      (size_t)embedding->dimensions[0] * sizeof(float)),
         tensor_crc32(0, (const uint8_t *)normalized,
                      (size_t)embedding->dimensions[0] * sizeof(float)),
         output_sum, sqrt(output_square_sum),
         (double)minimum, (double)maximum);
  printf("normalized[0..7]=");
  for (index = 0; index < embedding->dimensions[0] && index < 8; index++)
    {
      printf("%s%.7g", index == 0 ? "" : ",", (double)normalized[index]);
    }

  putchar('\n');

  if (projection_mode == 0)
    {
      puts("Qwen2 token embedding and layer-0 RMSNorm checkpoint passed; "
           "Q projection pending.");
      ret = 0;
      goto out;
    }

  q_output = malloc((size_t)attention_q->dimensions[1] * sizeof(float));
  if (q_output == NULL)
    {
      fputs("aipetllm: qproj output allocation failed\n", stderr);
      goto out;
    }

  if (read_quantized_matvec(stream, data_start + attention_q->offset,
                            attention_q, normalized, q_output) < 0)
    {
      fputs("aipetllm: qproj matrix-vector execution failed\n", stderr);
      goto out;
    }

  output_sum = 0.0;
  output_square_sum = 0.0;
  for (index = 0; index < attention_q->dimensions[1]; index++)
    {
      float value = q_output[index];

      output_sum += value;
      output_square_sum += (double)value * value;
      if (index == 0 || value < minimum)
        {
          minimum = value;
        }

      if (index == 0 || value > maximum)
        {
          maximum = value;
        }
    }

  printf("qproj tensor=%s type=%s input=%" PRIu64 " output=%" PRIu64
         " row-bytes=%" PRIu64 "\n",
         attention_q->name, tensor_type_name(attention_q->type),
         attention_q->dimensions[0], attention_q->dimensions[1],
         attention_q->bytes / attention_q->dimensions[1]);
  printf("q-crc=%08" PRIx32 " sum=%.9g l2=%.9g min=%.9g max=%.9g\n",
         tensor_crc32(0, (const uint8_t *)q_output,
                      (size_t)attention_q->dimensions[1] * sizeof(float)),
         output_sum, sqrt(output_square_sum),
         (double)minimum, (double)maximum);
  printf("q[0..7]=");
  for (index = 0; index < attention_q->dimensions[1] && index < 8; index++)
    {
      printf("%s%.7g", index == 0 ? "" : ",", (double)q_output[index]);
    }

  putchar('\n');

  if (projection_mode == 1)
    {
      puts("Qwen2 layer-0 Q projection checkpoint passed; "
           "K/V and RoPE pending.");
      ret = 0;
      goto out;
    }

  k_output = malloc((size_t)attention_k->dimensions[1] * sizeof(float));
  v_output = malloc((size_t)attention_v->dimensions[1] * sizeof(float));
  if (k_output == NULL || v_output == NULL)
    {
      fputs("aipetllm: qkv K/V allocation failed\n", stderr);
      goto out;
    }

  if (read_quantized_matvec(stream, data_start + attention_k->offset,
                            attention_k, normalized, k_output) < 0 ||
      read_quantized_matvec(stream, data_start + attention_v->offset,
                            attention_v, normalized, v_output) < 0 ||
      add_f32_bias(stream, data_start, attention_q_bias, q_output,
                   attention_q->dimensions[1]) < 0 ||
      add_f32_bias(stream, data_start, attention_k_bias, k_output,
                   attention_k->dimensions[1]) < 0 ||
      add_f32_bias(stream, data_start, attention_v_bias, v_output,
                   attention_v->dimensions[1]) < 0)
    {
      fputs("aipetllm: qkv projection/bias execution failed\n", stderr);
      goto out;
    }

  printf("qkv heads=%" PRIu32 "/%" PRIu32 " head-dim=%" PRIu64
         " position=%" PRIu32 " rope-dim=%" PRIu32 " base=%.9g\n",
         head_count, head_count_kv,
         attention_q->dimensions[1] / head_count, position,
         rotary_dimension, (double)frequency_base);
  printf("biased-crc q=%08" PRIx32 " k=%08" PRIx32 " v=%08" PRIx32
         " dimensions=%" PRIu64 "/%" PRIu64 "/%" PRIu64 "\n",
         tensor_crc32(0, (const uint8_t *)q_output,
                      (size_t)attention_q->dimensions[1] * sizeof(float)),
         tensor_crc32(0, (const uint8_t *)k_output,
                      (size_t)attention_k->dimensions[1] * sizeof(float)),
         tensor_crc32(0, (const uint8_t *)v_output,
                      (size_t)attention_v->dimensions[1] * sizeof(float)),
         attention_q->dimensions[1], attention_k->dimensions[1],
         attention_v->dimensions[1]);

  if (apply_normal_rope(q_output, head_count,
                        attention_q->dimensions[1] / head_count,
                        rotary_dimension, position, frequency_base) < 0 ||
      apply_normal_rope(k_output, head_count_kv,
                        attention_k->dimensions[1] / head_count_kv,
                        rotary_dimension, position, frequency_base) < 0)
    {
      fputs("aipetllm: qkv RoPE configuration/execution failed\n", stderr);
      goto out;
    }

  printf("rope-crc q=%08" PRIx32 " k=%08" PRIx32
         " v-unchanged=%08" PRIx32 "\n",
         tensor_crc32(0, (const uint8_t *)q_output,
                      (size_t)attention_q->dimensions[1] * sizeof(float)),
         tensor_crc32(0, (const uint8_t *)k_output,
                      (size_t)attention_k->dimensions[1] * sizeof(float)),
         tensor_crc32(0, (const uint8_t *)v_output,
                      (size_t)attention_v->dimensions[1] * sizeof(float)));
  puts("Qwen2 layer-0 biased Q/K/V and RoPE checkpoint passed; "
       "attention scores and KV cache pending.");

  if (projection_mode == 2)
    {
      ret = 0;
      goto out;
    }

  if (second_token_id >= embedding->dimensions[1])
    {
      fputs("aipetllm: attn2 second token is outside vocabulary\n", stderr);
      goto out;
    }

  second_input = malloc((size_t)embedding->dimensions[0] * sizeof(float));
  second_normalized = malloc((size_t)embedding->dimensions[0] *
                             sizeof(float));
  second_q = malloc((size_t)attention_q->dimensions[1] * sizeof(float));
  second_k = malloc((size_t)attention_k->dimensions[1] * sizeof(float));
  second_v = malloc((size_t)attention_v->dimensions[1] * sizeof(float));
  attention_output = malloc((size_t)attention_q->dimensions[1] *
                            sizeof(float));
  if (second_input == NULL || second_normalized == NULL ||
      second_q == NULL || second_k == NULL || second_v == NULL ||
      attention_output == NULL)
    {
      fputs("aipetllm: attn2 vector allocation failed\n", stderr);
      goto out;
    }

  if (normalized_token(stream, data_start, embedding, weight, epsilon,
                       second_token_id, second_input,
                       second_normalized) < 0 ||
      read_quantized_matvec(stream, data_start + attention_q->offset,
                            attention_q, second_normalized, second_q) < 0 ||
      read_quantized_matvec(stream, data_start + attention_k->offset,
                            attention_k, second_normalized, second_k) < 0 ||
      read_quantized_matvec(stream, data_start + attention_v->offset,
                            attention_v, second_normalized, second_v) < 0 ||
      add_f32_bias(stream, data_start, attention_q_bias, second_q,
                   attention_q->dimensions[1]) < 0 ||
      add_f32_bias(stream, data_start, attention_k_bias, second_k,
                   attention_k->dimensions[1]) < 0 ||
      add_f32_bias(stream, data_start, attention_v_bias, second_v,
                   attention_v->dimensions[1]) < 0 ||
      apply_normal_rope(second_q, head_count,
                        attention_q->dimensions[1] / head_count,
                        rotary_dimension, 1, frequency_base) < 0 ||
      apply_normal_rope(second_k, head_count_kv,
                        attention_k->dimensions[1] / head_count_kv,
                        rotary_dimension, 1, frequency_base) < 0)
    {
      fputs("aipetllm: attn2 second-token QKV execution failed\n", stderr);
      goto out;
    }

  {
    uint64_t head_dimension = attention_q->dimensions[1] / head_count;
    uint32_t query_per_kv = head_count / head_count_kv;
    float score_values[64];
    float probability_values[64];
    float scale = 1.0f / sqrtf((float)head_dimension);
    double context_sum = 0.0;
    double context_square_sum = 0.0;
    float context_minimum = 0.0f;
    float context_maximum = 0.0f;
    uint32_t head;

    if (head_count > 32 || head_count % head_count_kv != 0)
      {
        fputs("aipetllm: attn2 unsupported GQA head mapping\n", stderr);
        goto out;
      }

    for (head = 0; head < head_count; head++)
      {
        uint32_t kv_head = head / query_per_kv;
        uint64_t query_base = (uint64_t)head * head_dimension;
        uint64_t kv_base = (uint64_t)kv_head * head_dimension;
        double score0 = 0.0;
        double score1 = 0.0;
        float maximum_score;
        float exponent0;
        float exponent1;
        float denominator;
        uint64_t component;

        for (component = 0; component < head_dimension; component++)
          {
            float query = second_q[query_base + component];

            score0 += (double)query * k_output[kv_base + component];
            score1 += (double)query * second_k[kv_base + component];
          }

        score_values[head * 2] = (float)score0 * scale;
        score_values[head * 2 + 1] = (float)score1 * scale;
        maximum_score = fmaxf(score_values[head * 2],
                              score_values[head * 2 + 1]);
        exponent0 = expf(score_values[head * 2] - maximum_score);
        exponent1 = expf(score_values[head * 2 + 1] - maximum_score);
        denominator = exponent0 + exponent1;
        probability_values[head * 2] = exponent0 / denominator;
        probability_values[head * 2 + 1] = exponent1 / denominator;

        for (component = 0; component < head_dimension; component++)
          {
            uint64_t output_index = query_base + component;
            float value = probability_values[head * 2] *
                          v_output[kv_base + component] +
                          probability_values[head * 2 + 1] *
                          second_v[kv_base + component];

            attention_output[output_index] = value;
            context_sum += value;
            context_square_sum += (double)value * value;
            if (output_index == 0 || value < context_minimum)
              {
                context_minimum = value;
              }

            if (output_index == 0 || value > context_maximum)
              {
                context_maximum = value;
              }
          }
      }

    printf("kv-cache tokens=2 kv-heads=%" PRIu32 " head-dim=%" PRIu64
           " k-crc=%08" PRIx32 "/%08" PRIx32
           " v-crc=%08" PRIx32 "/%08" PRIx32 "\n",
           head_count_kv, head_dimension,
           tensor_crc32(0, (const uint8_t *)k_output,
                        (size_t)attention_k->dimensions[1] * sizeof(float)),
           tensor_crc32(0, (const uint8_t *)second_k,
                        (size_t)attention_k->dimensions[1] * sizeof(float)),
           tensor_crc32(0, (const uint8_t *)v_output,
                        (size_t)attention_v->dimensions[1] * sizeof(float)),
           tensor_crc32(0, (const uint8_t *)second_v,
                        (size_t)attention_v->dimensions[1] * sizeof(float)));
    printf("attention query-position=1 causal-keys=2 group=%" PRIu32
           " scale=%.9g score-crc=%08" PRIx32
           " probability-crc=%08" PRIx32 "\n",
           query_per_kv, (double)scale,
           tensor_crc32(0, (const uint8_t *)score_values,
                        (size_t)head_count * 2 * sizeof(float)),
           tensor_crc32(0, (const uint8_t *)probability_values,
                        (size_t)head_count * 2 * sizeof(float)));
    for (head = 0; head < head_count && head < 3; head++)
      {
        printf("head[%" PRIu32 "] scores=%.7g,%.7g probs=%.7g,%.7g\n",
               head, (double)score_values[head * 2],
               (double)score_values[head * 2 + 1],
               (double)probability_values[head * 2],
               (double)probability_values[head * 2 + 1]);
      }

    printf("context-crc=%08" PRIx32
           " sum=%.9g l2=%.9g min=%.9g max=%.9g\n",
           tensor_crc32(0, (const uint8_t *)attention_output,
                        (size_t)attention_q->dimensions[1] * sizeof(float)),
           context_sum, sqrt(context_square_sum),
           (double)context_minimum, (double)context_maximum);
  }

  puts("Qwen2 two-token causal GQA/KV-cache checkpoint passed; "
       "attention output projection pending.");

  if (projection_mode == 3)
    {
      ret = 0;
      goto out;
    }

  projected_output = malloc((size_t)embedding->dimensions[0] *
                            sizeof(float));
  residual_output = malloc((size_t)embedding->dimensions[0] * sizeof(float));
  post_attention = malloc((size_t)embedding->dimensions[0] * sizeof(float));
  ffn_norm_weight = malloc((size_t)embedding->dimensions[0] * sizeof(float));
  if (projected_output == NULL || residual_output == NULL ||
      post_attention == NULL || ffn_norm_weight == NULL)
    {
      fputs("aipetllm: attnblock2 vector allocation failed\n", stderr);
      goto out;
    }

  if (read_quantized_matvec(stream,
                            data_start + attention_output_weight->offset,
                            attention_output_weight, attention_output,
                            projected_output) < 0 ||
      read_dequantized_row(stream, data_start + ffn_norm->offset,
                           ffn_norm, 0, ffn_norm_weight) < 0)
    {
      fputs("aipetllm: attnblock2 output/norm tensor execution failed\n",
            stderr);
      goto out;
    }

  {
    double residual_square_sum = 0.0;
    double normalized_sum = 0.0;
    double normalized_square_sum = 0.0;
    float post_inverse_rms;
    float post_minimum = 0.0f;
    float post_maximum = 0.0f;
    uint64_t component;

    for (component = 0; component < embedding->dimensions[0]; component++)
      {
        float value = second_input[component] + projected_output[component];

        residual_output[component] = value;
        residual_square_sum += (double)value * value;
      }

    post_inverse_rms = 1.0f /
                       sqrtf((float)(residual_square_sum /
                                     embedding->dimensions[0]) + epsilon);
    for (component = 0; component < embedding->dimensions[0]; component++)
      {
        float value = residual_output[component] * post_inverse_rms *
                      ffn_norm_weight[component];

        post_attention[component] = value;
        normalized_sum += value;
        normalized_square_sum += (double)value * value;
        if (component == 0 || value < post_minimum)
          {
            post_minimum = value;
          }

        if (component == 0 || value > post_maximum)
          {
            post_maximum = value;
          }
      }

    printf("attention-output tensor=%s type=%s input=%" PRIu64
           " output=%" PRIu64 " row-bytes=%" PRIu64 "\n",
           attention_output_weight->name,
           tensor_type_name(attention_output_weight->type),
           attention_output_weight->dimensions[0],
           attention_output_weight->dimensions[1],
           attention_output_weight->bytes /
           attention_output_weight->dimensions[1]);
    printf("attention-output-crc=%08" PRIx32
           " residual-crc=%08" PRIx32
           " post-norm-crc=%08" PRIx32 "\n",
           tensor_crc32(0, (const uint8_t *)projected_output,
                        (size_t)embedding->dimensions[0] * sizeof(float)),
           tensor_crc32(0, (const uint8_t *)residual_output,
                        (size_t)embedding->dimensions[0] * sizeof(float)),
           tensor_crc32(0, (const uint8_t *)post_attention,
                        (size_t)embedding->dimensions[0] * sizeof(float)));
    printf("post-attention-rms=%.9g inverse-rms=%.9g"
           " sum=%.9g l2=%.9g min=%.9g max=%.9g\n",
           sqrt(residual_square_sum / embedding->dimensions[0]),
           (double)post_inverse_rms, normalized_sum,
           sqrt(normalized_square_sum),
           (double)post_minimum, (double)post_maximum);
  }

  puts("Qwen2 layer-0 attention output/residual/FFN-norm checkpoint passed; "
       "SwiGLU feed-forward network pending.");

  if (projection_mode == 4)
    {
      ret = 0;
      goto out;
    }

  ffn_gate_output = malloc((size_t)ffn_gate->dimensions[1] * sizeof(float));
  ffn_up_output = malloc((size_t)ffn_up->dimensions[1] * sizeof(float));
  ffn_swiglu = malloc((size_t)ffn_gate->dimensions[1] * sizeof(float));
  ffn_down_output = malloc((size_t)embedding->dimensions[0] * sizeof(float));
  block_output = malloc((size_t)embedding->dimensions[0] * sizeof(float));
  if (ffn_gate_output == NULL || ffn_up_output == NULL ||
      ffn_swiglu == NULL || ffn_down_output == NULL ||
      block_output == NULL)
    {
      fputs("aipetllm: block2 FFN vector allocation failed\n", stderr);
      goto out;
    }

  if (read_quantized_matvec(stream, data_start + ffn_gate->offset,
                            ffn_gate, post_attention,
                            ffn_gate_output) < 0 ||
      read_quantized_matvec(stream, data_start + ffn_up->offset,
                            ffn_up, post_attention, ffn_up_output) < 0)
    {
      fputs("aipetllm: block2 gate/up execution failed\n", stderr);
      goto out;
    }

  for (index = 0; index < ffn_gate->dimensions[1]; index++)
    {
      float gate = ffn_gate_output[index];
      float silu;

      if (gate >= 0.0f)
        {
          silu = gate / (1.0f + expf(-gate));
        }
      else
        {
          float exponential = expf(gate);
          silu = gate * exponential / (1.0f + exponential);
        }

      ffn_swiglu[index] = silu * ffn_up_output[index];
    }

  if (read_quantized_matvec(stream, data_start + ffn_down->offset,
                            ffn_down, ffn_swiglu,
                            ffn_down_output) < 0)
    {
      fputs("aipetllm: block2 down projection failed\n", stderr);
      goto out;
    }

  {
    double block_sum = 0.0;
    double block_square_sum = 0.0;
    float block_minimum = 0.0f;
    float block_maximum = 0.0f;

    for (index = 0; index < embedding->dimensions[0]; index++)
      {
        float value = residual_output[index] + ffn_down_output[index];

        block_output[index] = value;
        block_sum += value;
        block_square_sum += (double)value * value;
        if (index == 0 || value < block_minimum)
          {
            block_minimum = value;
          }

        if (index == 0 || value > block_maximum)
          {
            block_maximum = value;
          }
      }

    printf("swiglu gate=%s/%s up=%s/%s hidden=%" PRIu64 "\n",
           ffn_gate->name, tensor_type_name(ffn_gate->type),
           ffn_up->name, tensor_type_name(ffn_up->type),
           ffn_gate->dimensions[1]);
    printf("ffn-crc gate=%08" PRIx32 " up=%08" PRIx32
           " swiglu=%08" PRIx32 " down=%08" PRIx32 "\n",
           tensor_crc32(0, (const uint8_t *)ffn_gate_output,
                        (size_t)ffn_gate->dimensions[1] * sizeof(float)),
           tensor_crc32(0, (const uint8_t *)ffn_up_output,
                        (size_t)ffn_up->dimensions[1] * sizeof(float)),
           tensor_crc32(0, (const uint8_t *)ffn_swiglu,
                        (size_t)ffn_gate->dimensions[1] * sizeof(float)),
           tensor_crc32(0, (const uint8_t *)ffn_down_output,
                        (size_t)embedding->dimensions[0] * sizeof(float)));
    printf("block-output-crc=%08" PRIx32
           " sum=%.9g l2=%.9g min=%.9g max=%.9g\n",
           tensor_crc32(0, (const uint8_t *)block_output,
                        (size_t)embedding->dimensions[0] * sizeof(float)),
           block_sum, sqrt(block_square_sum),
           (double)block_minimum, (double)block_maximum);
  }

  puts("Qwen2 layer-0 full Transformer block checkpoint passed; "
       "multi-layer execution pending.");
  ret = 0;

out:
  free(block_output);
  free(ffn_down_output);
  free(ffn_swiglu);
  free(ffn_up_output);
  free(ffn_gate_output);
  free(ffn_norm_weight);
  free(post_attention);
  free(residual_output);
  free(projected_output);
  free(attention_output);
  free(second_v);
  free(second_k);
  free(second_q);
  free(second_normalized);
  free(second_input);
  free(v_output);
  free(k_output);
  free(q_output);
  free(normalized);
  free(weight);
  free(input);
  free(tensors);
  if (stream != NULL)
    {
      fclose(stream);
    }

  if (projection_mode >= 7 && model_memory != NULL)
    {
      if (ret == 0)
        {
          clock_gettime(CLOCK_MONOTONIC, &finished);
          printf("forwardfast: compute=%.4f sec; causal forward checkpoint; "
                 "model resident; %s\n",
                 (double)(finished.tv_sec - compute_start.tv_sec) +
                 (double)(finished.tv_nsec - compute_start.tv_nsec) / 1.0e9,
                 sequence != NULL ? "bounded generation finished" :
                                    "generation available via generateids");
        }

      if (!cache_hit)
        {
          if (ret == 0)
            {
              g_model_cache = model_memory;
              g_model_bytes = model_length;
              strcpy(g_model_path, path);
              puts("forwardfast: cache retained; use aipetllm unload to free");
            }
          else
            {
              free(model_memory);
            }
        }
    }

  if (cache_locked)
    {
      if (sequence != NULL)
        {
          __atomic_store_n(&g_generation_active, 0, __ATOMIC_RELEASE);
        }
      pthread_mutex_unlock(&g_model_lock);
      pthread_setcancelstate(old_cancel, NULL);
    }

  return ret;
}

static int embedding_pipeline_checkpoint(const char *path, uint32_t token_id,
                                          int mode, uint32_t position,
                                          uint32_t second_token)
{
  return embedding_pipeline_run(path, token_id, mode, position, second_token,
                                NULL);
}

int aipetllm_sequence_ram_checkpoint(const char *path,
                                    struct aipetllm_sequence_s *sequence,
                                    unsigned int mask)
{
  if (sequence == NULL || sequence->input_count == 0 ||
      sequence->input_count > AIPETLLM_INPUT_LIMIT || sequence->generate_limit > 64)
    {
      return 1;
    }

  sequence->eos = UINT32_MAX;
  return embedding_pipeline_run(path, sequence->input[0],
                                9 | (int)(mask << 8), 0, 0, sequence);
}

int aipetllm_embedding_checkpoint(const char *path, uint32_t token_id)
{
  return embedding_pipeline_checkpoint(path, token_id, 0, 0, 0);
}

int aipetllm_q_projection_checkpoint(const char *path, uint32_t token_id)
{
  return embedding_pipeline_checkpoint(path, token_id, 1, 0, 0);
}

int aipetllm_qkv_checkpoint(const char *path, uint32_t token_id,
                            uint32_t position)
{
  return embedding_pipeline_checkpoint(path, token_id, 2, position, 0);
}

int aipetllm_attention2_checkpoint(const char *path, uint32_t first_token,
                                   uint32_t second_token)
{
  return embedding_pipeline_checkpoint(path, first_token, 3, 0,
                                       second_token);
}

int aipetllm_attention_block2_checkpoint(const char *path,
                                         uint32_t first_token,
                                         uint32_t second_token)
{
  return embedding_pipeline_checkpoint(path, first_token, 4, 0,
                                       second_token);
}

int aipetllm_transformer_block2_checkpoint(const char *path,
                                           uint32_t first_token,
                                           uint32_t second_token)
{
  return embedding_pipeline_checkpoint(path, first_token, 5, 0,
                                       second_token);
}

int aipetllm_forward1_checkpoint(const char *path, uint32_t token_id)
{
  return embedding_pipeline_checkpoint(path, token_id, 6, 0, 0);
}

int aipetllm_forward_ram_checkpoint(const char *path, uint32_t token_id,
                                   unsigned int cpu_mask)
{
  return embedding_pipeline_checkpoint(path, token_id,
                                       7 | (int)(cpu_mask << 8), 0, 0);
}

int aipetllm_forward2_ram_checkpoint(const char *path, uint32_t first_token,
                                    uint32_t second_token,
                                    unsigned int cpu_mask)
{
  return embedding_pipeline_checkpoint(path, first_token,
                                       8 | (int)(cpu_mask << 8), 0,
                                       second_token);
}
