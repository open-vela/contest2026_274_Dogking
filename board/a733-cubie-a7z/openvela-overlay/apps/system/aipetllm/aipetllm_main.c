/****************************************************************************
 * apps/system/aipetllm/aipetllm_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "aipetllm_sequence.h"

#define GGUF_MAGIC UINT32_C(0x46554747)
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

#define MODEL_DEFAULT CONFIG_AIPETLLM_DEFAULT_MODEL
#define MODEL_KEY_LIMIT 256
#define MODEL_VALUE_LIMIT 512
#define PROBE_CHUNK (16 * 1024 * 1024)
#define READCHECK_CHUNK (1024 * 1024)

struct gguf_reader_s
{
  FILE *stream;
  uint32_t version;
  uint64_t tensors;
  uint64_t metadata;
};

extern int aipetllm_cxx_checkpoint(void);
extern int aipetllm_cpu_checkpoint(void);
extern int aipetllm_cache_control(int unload);
extern int aipetllm_forward_fast_checkpoint(const char *path,
                                           uint32_t token_id,
                                           unsigned int cpu_mask);
extern int aipetllm_ggml_quant_checkpoint(void);
extern int aipetllm_model_checkpoint(const char *path);
extern int aipetllm_embedding_checkpoint(const char *path,
                                         uint32_t token_id);
extern int aipetllm_q_projection_checkpoint(const char *path,
                                            uint32_t token_id);
extern int aipetllm_qkv_checkpoint(const char *path, uint32_t token_id,
                                   uint32_t position);
extern int aipetllm_attention2_checkpoint(const char *path,
                                          uint32_t first_token,
                                          uint32_t second_token);
extern int aipetllm_attention_block2_checkpoint(const char *path,
                                                 uint32_t first_token,
                                                 uint32_t second_token);
extern int aipetllm_transformer_block2_checkpoint(const char *path,
                                                   uint32_t first_token,
                                                   uint32_t second_token);
extern int aipetllm_forward1_checkpoint(const char *path,
                                         uint32_t token_id);
extern int aipetllm_tokenizer_checkpoint(const char *path);
extern int aipetllm_decode_checkpoint(const char *path, int id_count,
                                      char *const id_text[]);
extern int aipetllm_bpe_checkpoint(const char *path, const char *prompt);
extern int aipetllm_encode_tokens(const char *, const char *, uint32_t *,
                                  uint32_t, uint32_t *);
extern int aipetllm_chat_tokens(const char *, const char *, uint32_t *,
                                uint32_t, uint32_t *, uint32_t *);
extern int aipetllm_chat_continue_tokens(const char *, const char *,
 const uint32_t *, uint32_t, uint32_t *, uint32_t, uint32_t *, uint32_t *);
extern void *aipetllm_stream_open(const char *);
extern int aipetllm_stream_emit(void *, uint32_t);
extern void aipetllm_stream_close(void *);

static pthread_mutex_t g_chat_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_chat_history[AIPETLLM_SEQUENCE_LIMIT];
static uint32_t g_chat_history_count;
static char g_chat_model[MODEL_VALUE_LIMIT];

static const char *file_type(mode_t mode)
{
  if (S_ISDIR(mode))
    {
      return "directory";
    }

  if (S_ISREG(mode))
    {
      return "regular-file";
    }

  return "other";
}

/* Strict bounded decimal CSV: also used for core IDs, rejecting repeats there. */
static int parse_id_list(const char *text, uint32_t *ids, size_t capacity,
                         uint32_t *count)
{
  const char *cursor = text;
  *count = 0;
  for (;;)
    {
      char *end;
      unsigned long value;
      if (*count == capacity || *cursor < '0' || *cursor > '9')
        {
          return 1;
        }

      errno = 0;
      value = strtoul(cursor, &end, 10);
      if (errno == ERANGE || value > UINT32_MAX ||
          (*end != ',' && *end != '\0'))
        {
          return 1;
        }

      ids[(*count)++] = (uint32_t)value;
      if (*end == '\0')
        {
          return 0;
        }

      cursor = end + 1;
    }
}

static int generate_ids_locked(int argc, char *argv[])
{
  struct aipetllm_sequence_s sequence = {0};
  uint32_t limit[1];
  uint32_t count;
  unsigned int mask = 0xfc;
  char id_text[AIPETLLM_SEQUENCE_LIMIT][11];
  char *id_pointer[AIPETLLM_SEQUENCE_LIMIT];
  uint32_t index;
  int result;

  if (parse_id_list(argv[4], limit, 1, &count) ||
      limit[0] > AIPETLLM_SEQUENCE_LIMIT)
    {
      fputs("generateids: prompt requires 1..64 IDs; count must be 0..64\n",
            stderr);
      return 1;
    }

  if (strcmp(argv[1], "chat") == 0 || strcmp(argv[1], "ask") == 0)
    {
      sequence.chat_mode = 1;
      if (strcmp(argv[1], "ask") == 0 && g_chat_history_count != 0)
        {
          if (strcmp(g_chat_model, argv[2]) != 0)
            {
              fputs("ask: model changed; use newchat\n", stderr);
              return 1;
            }

          result = aipetllm_chat_continue_tokens(argv[2], argv[3],
            g_chat_history, g_chat_history_count, sequence.input,
            AIPETLLM_SEQUENCE_LIMIT, &sequence.input_count, &sequence.chat_stop);
        }
      else
        {
      result = aipetllm_chat_tokens(argv[2], argv[3], sequence.input,
                                    AIPETLLM_SEQUENCE_LIMIT,
                                    &sequence.input_count, &sequence.chat_stop);
        }
    }
  else if (strcmp(argv[1], "generate") == 0)
    {
      result = aipetllm_encode_tokens(argv[2], argv[3], sequence.input,
                                      AIPETLLM_SEQUENCE_LIMIT,
                                      &sequence.input_count);
    }
  else
    {
      result = parse_id_list(argv[3], sequence.input, AIPETLLM_SEQUENCE_LIMIT,
                              &sequence.input_count);
    }

  if (result != 0 || sequence.input_count == 0)
    {
      fputs("generate: invalid or overlong prompt\n", stderr);
      return 1;
    }

  sequence.generate_limit = limit[0];
  if (argc == 6)
    {
      uint32_t cores[8];
      if (parse_id_list(argv[5], cores, 8, &count))
        {
          return 1;
        }

      mask = 0;
      for (index = 0; index < count; index++)
        {
          if (cores[index] > 7 || (mask & (1u << cores[index])))
            {
              fputs("generateids: cores must be unique IDs 0..7\n", stderr);
              return 1;
            }

          mask |= 1u << cores[index];
        }
    }

  if (sequence.chat_mode && sequence.generate_limit != 0)
    {
      sequence.emit_context = aipetllm_stream_open(argv[2]);
      if (sequence.emit_context == NULL) return 1;
      sequence.emit = aipetllm_stream_emit;
    }

  result = aipetllm_sequence_fast_checkpoint(argv[2], &sequence, mask);
  if (sequence.emit_context != NULL)
    {
      aipetllm_stream_close(sequence.emit_context);
    }

  if (result == 0)
    {
      printf("llm-timing: first-prediction=%.4f sec after-first=%.4f sec "
             "text-tokens=%" PRIu32 " (load/tokenizer excluded)\n",
             sequence.first_token_seconds, sequence.after_first_seconds,
             sequence.output_count);
    }

  if (result == 0 && sequence.output_count != 0 &&
      strcmp(argv[1], "ask") == 0)
    {
      uint32_t needed = sequence.input_count + sequence.output_count + 1;
      if (needed > AIPETLLM_SEQUENCE_LIMIT ||
          strlen(argv[2]) >= sizeof(g_chat_model))
        {
          puts("ask: turn not retained (history capacity); use newchat");
        }
      else
        {
          memcpy(g_chat_history, sequence.input,
                 sequence.input_count * sizeof(uint32_t));
          memcpy(g_chat_history + sequence.input_count, sequence.output,
                 sequence.output_count * sizeof(uint32_t));
          g_chat_history[needed - 1] = sequence.chat_stop;
          g_chat_history_count = needed;
          strcpy(g_chat_model, argv[2]);
          printf("ask: retained=%" PRIu32 "/64 tokens; KV recomputed per turn\n",
                 needed);
        }
    }

  if (result != 0 || sequence.output_count == 0)
    {
      return result;
    }

  if (sequence.emit != NULL) return result;

  for (index = 0; index < sequence.output_count; index++)
    {
      snprintf(id_text[index], sizeof(id_text[index]), "%" PRIu32,
               sequence.output[index]);
      id_pointer[index] = id_text[index];
    }

  return aipetllm_decode_checkpoint(argv[2], sequence.output_count, id_pointer);
}

static int generate_ids(int argc, char *argv[])
{
  int result;
  int old_cancel;
  if (strcmp(argv[1], "ask") != 0) return generate_ids_locked(argc, argv);
  if (pthread_mutex_trylock(&g_chat_lock) != 0)
    {
      fputs("ask: session busy\n", stderr);
      return 1;
    }
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_cancel);
  result = generate_ids_locked(argc, argv);
  pthread_mutex_unlock(&g_chat_lock);
  pthread_setcancelstate(old_cancel, NULL);
  return result;
}

static int path_check(const char *path)
{
  char component[MODEL_VALUE_LIMIT];
  struct stat info;
  size_t length;
  size_t index;

  length = strlen(path);
  if (length == 0 || length >= sizeof(component))
    {
      fprintf(stderr, "aipetllm: invalid path length %lu\n",
              (unsigned long)length);
      return 1;
    }

  memcpy(component, path, length + 1);
  printf("path-check target=%s\n", path);

  for (index = 1; index <= length; index++)
    {
      char saved;

      if (component[index] != '/' && component[index] != '\0')
        {
          continue;
        }

      saved = component[index];
      component[index] = '\0';
      if (component[0] != '\0')
        {
          if (stat(component, &info) < 0)
            {
              int error = errno;
              printf("FAIL path=%s errno=%d (%s)\n", component, error,
                     strerror(error));
              component[index] = saved;
              return 1;
            }

          printf("OK path=%s type=%s size=%" PRIu64 "\n", component,
                 file_type(info.st_mode), (uint64_t)info.st_size);
          if (saved == '/' && !S_ISDIR(info.st_mode))
            {
              printf("FAIL path=%s must be a directory\n", component);
              component[index] = saved;
              return 1;
            }
        }

      component[index] = saved;
    }

  if (!S_ISREG(info.st_mode))
    {
      printf("FAIL target is %s, expected regular-file\n",
             file_type(info.st_mode));
      return 1;
    }

  puts("path-check passed");
  return 0;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data,
                             size_t length)
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

static int read_check(const char *path)
{
  struct stat info;
  uint8_t *buffer;
  FILE *stream;
  uint64_t total = 0;
  uint32_t crc = 0;

  if (path_check(path) != 0 || stat(path, &info) < 0)
    {
      return 1;
    }

  buffer = malloc(READCHECK_CHUNK);
  if (buffer == NULL)
    {
      fputs("aipetllm: cannot allocate read-check buffer\n", stderr);
      return 1;
    }

  stream = fopen(path, "rb");
  if (stream == NULL)
    {
      fprintf(stderr, "aipetllm: open %s failed: %d (%s)\n", path, errno,
              strerror(errno));
      free(buffer);
      return 1;
    }

  for (;;)
    {
      size_t count = fread(buffer, 1, READCHECK_CHUNK, stream);

      if (count != 0)
        {
          crc = crc32_update(crc, buffer, count);
          total += count;
        }

      if (count != READCHECK_CHUNK)
        {
          if (ferror(stream))
            {
              fprintf(stderr, "aipetllm: read failed after %" PRIu64
                              " bytes\n", total);
              fclose(stream);
              free(buffer);
              return 1;
            }

          break;
        }
    }

  fclose(stream);
  free(buffer);
  printf("read-check passed bytes=%" PRIu64 " expected=%" PRIu64
         " crc32=%08" PRIx32 "\n", total, (uint64_t)info.st_size, crc);
  return total == (uint64_t)info.st_size ? 0 : 1;
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
  uint64_t i;
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

  for (i = 0; i < count; i++)
    {
      int ret = skip_value(stream, element_type, depth + 1);
      if (ret < 0)
        {
          return ret;
        }
    }

  return 0;
}

static int print_value(FILE *stream, uint32_t type)
{
  char text[MODEL_VALUE_LIMIT];
  uint64_t length;
  uint64_t u64;
  int64_t i64;
  uint32_t u32;
  int32_t i32;
  float f32;
  double f64;
  uint8_t u8;

  switch (type)
    {
      case GGUF_TYPE_STRING:
        if (read_string(stream, text, sizeof(text), &length) < 0)
          {
            return -EIO;
          }

        printf("'%s'%s", text,
               length >= sizeof(text) ? " (truncated)" : "");
        return 0;
      case GGUF_TYPE_UINT8:
      case GGUF_TYPE_BOOL:
        if (read_exact(stream, &u8, sizeof(u8)) < 0) return -EIO;
        printf("%u", (unsigned int)u8);
        return 0;
      case GGUF_TYPE_UINT32:
        if (read_exact(stream, &u32, sizeof(u32)) < 0) return -EIO;
        printf("%" PRIu32, u32);
        return 0;
      case GGUF_TYPE_INT32:
        if (read_exact(stream, &i32, sizeof(i32)) < 0) return -EIO;
        printf("%" PRId32, i32);
        return 0;
      case GGUF_TYPE_UINT64:
        if (read_exact(stream, &u64, sizeof(u64)) < 0) return -EIO;
        printf("%" PRIu64, u64);
        return 0;
      case GGUF_TYPE_INT64:
        if (read_exact(stream, &i64, sizeof(i64)) < 0) return -EIO;
        printf("%" PRId64, i64);
        return 0;
      case GGUF_TYPE_FLOAT32:
        if (read_exact(stream, &f32, sizeof(f32)) < 0) return -EIO;
        printf("%.6g", (double)f32);
        return 0;
      case GGUF_TYPE_FLOAT64:
        if (read_exact(stream, &f64, sizeof(f64)) < 0) return -EIO;
        printf("%.9g", f64);
        return 0;
      default:
        return skip_value(stream, type, 0);
    }
}

static int interesting_key(const char *key)
{
  return strcmp(key, "general.architecture") == 0 ||
         strcmp(key, "general.name") == 0 ||
         strcmp(key, "general.file_type") == 0 ||
         strcmp(key, "general.quantization_version") == 0 ||
         strstr(key, ".context_length") != NULL ||
         strstr(key, ".block_count") != NULL ||
         strstr(key, ".embedding_length") != NULL ||
         strstr(key, ".attention.head_count") != NULL ||
         strcmp(key, "tokenizer.ggml.model") == 0 ||
         strcmp(key, "tokenizer.ggml.tokens") == 0;
}

static int inspect_gguf(const char *path)
{
  struct gguf_reader_s reader;
  struct stat info;
  uint32_t magic;
  uint64_t i;
  int ret;

  memset(&reader, 0, sizeof(reader));
  if (stat(path, &info) < 0)
    {
      int error = errno;
      fprintf(stderr, "aipetllm: stat %s failed: %d (%s)\n", path, error,
              strerror(error));
      path_check(path);
      return 1;
    }

  reader.stream = fopen(path, "rb");
  if (reader.stream == NULL)
    {
      fprintf(stderr, "aipetllm: open %s failed: %d\n", path, errno);
      return 1;
    }

  ret = read_exact(reader.stream, &magic, sizeof(magic));
  ret |= read_exact(reader.stream, &reader.version, sizeof(reader.version));
  ret |= read_exact(reader.stream, &reader.tensors, sizeof(reader.tensors));
  ret |= read_exact(reader.stream, &reader.metadata, sizeof(reader.metadata));
  if (ret < 0 || magic != GGUF_MAGIC ||
      reader.version < 2 || reader.version > 3)
    {
      fprintf(stderr, "aipetllm: invalid/unsupported GGUF header\n");
      fclose(reader.stream);
      return 1;
    }

  printf("GGUF path=%s\n", path);
  printf("size=%" PRIu64 " bytes version=%" PRIu32
         " tensors=%" PRIu64 " metadata=%" PRIu64 "\n",
         (uint64_t)info.st_size, reader.version,
         reader.tensors, reader.metadata);

  for (i = 0; i < reader.metadata; i++)
    {
      char key[MODEL_KEY_LIMIT];
      uint64_t key_length;
      uint32_t type;

      ret = read_string(reader.stream, key, sizeof(key), &key_length);
      if (ret < 0 || key_length >= sizeof(key) ||
          read_exact(reader.stream, &type, sizeof(type)) < 0)
        {
          fprintf(stderr, "aipetllm: malformed metadata at index %" PRIu64
                          "\n", i);
          fclose(reader.stream);
          return 1;
        }

      if (interesting_key(key) && type != GGUF_TYPE_ARRAY)
        {
          printf("%s=", key);
          ret = print_value(reader.stream, type);
          putchar('\n');
        }
      else if (strcmp(key, "tokenizer.ggml.tokens") == 0 &&
               type == GGUF_TYPE_ARRAY)
        {
          uint32_t element_type;
          uint64_t count;

          if (read_exact(reader.stream, &element_type,
                         sizeof(element_type)) < 0 ||
              read_exact(reader.stream, &count, sizeof(count)) < 0)
            {
              ret = -EIO;
            }
          else
            {
              printf("%s.count=%" PRIu64 "\n", key, count);
              ret = 0;
              for (uint64_t token = 0; token < count && ret == 0; token++)
                {
                  ret = skip_value(reader.stream, element_type, 1);
                }
            }
        }
      else
        {
          ret = skip_value(reader.stream, type, 0);
        }

      if (ret < 0)
        {
          fprintf(stderr, "aipetllm: unsupported metadata '%s' type=%" PRIu32
                          " (%d)\n", key, type, ret);
          fclose(reader.stream);
          return 1;
        }
    }

  puts("GGUF metadata checkpoint passed; tensor loading/inference pending.");
  fclose(reader.stream);
  return 0;
}

static int memory_probe(unsigned long target_mib)
{
  void **chunks;
  size_t chunk_count;
  size_t allocated = 0;
  size_t index;

  if (target_mib == 0 || target_mib > 3072)
    {
      fputs("aipetllm: probe range is 1..3072 MiB\n", stderr);
      return 1;
    }

  chunk_count = ((size_t)target_mib * 1024 * 1024 + PROBE_CHUNK - 1) /
                PROBE_CHUNK;
  chunks = calloc(chunk_count, sizeof(*chunks));
  if (chunks == NULL)
    {
      fputs("aipetllm: cannot allocate probe table\n", stderr);
      return 1;
    }

  for (index = 0; index < chunk_count; index++)
    {
      size_t remaining = (size_t)target_mib * 1024 * 1024 - allocated;
      size_t length = remaining < PROBE_CHUNK ? remaining : PROBE_CHUNK;
      size_t offset;

      chunks[index] = malloc(length);
      if (chunks[index] == NULL)
        {
          break;
        }

      for (offset = 0; offset < length; offset += 4096)
        {
          ((volatile uint8_t *)chunks[index])[offset] =
            (uint8_t)(index ^ offset);
        }

      allocated += length;
    }

  printf("memory-probe requested=%lu MiB allocated-touched=%lu MiB "
         "chunks=%lu/%lu\n", target_mib,
         (unsigned long)(allocated / (1024 * 1024)),
         (unsigned long)index, (unsigned long)chunk_count);

  while (index != 0)
    {
      free(chunks[--index]);
    }

  free(chunks);
  return allocated == (size_t)target_mib * 1024 * 1024 ? 0 : 1;
}

static void usage(void)
{
  puts("AI Pet local LLM stage 1");
  puts("Usage:");
  puts("  aipetllm info");
  puts("  aipetllm probe [MiB]");
  puts("  aipetllm cxxcheck");
  puts("  aipetllm cpucheck");
  puts("  aipetllm quantcheck");
  puts("  aipetllm pathcheck [model.gguf]");
  puts("  aipetllm readcheck [model.gguf]");
  puts("  aipetllm gguf [model.gguf]");
  puts("  aipetllm modelcheck [model.gguf]");
  puts("  aipetllm tokencheck [model.gguf]");
  puts("  aipetllm decode model.gguf token-id [token-id ...]");
  puts("  aipetllm encode model.gguf prompt");
  puts("  aipetllm embed model.gguf token-id");
  puts("  aipetllm qproj model.gguf token-id");
  puts("  aipetllm qkv model.gguf token-id position");
  puts("  aipetllm attn2 model.gguf first-token-id second-token-id");
  puts("  aipetllm attnblock2 model.gguf first-token-id second-token-id");
  puts("  aipetllm block2 model.gguf first-token-id second-token-id");
  puts("  aipetllm forward1 model.gguf token-id");
  puts("  aipetllm forwardfast model.gguf token-id [cores: 2,3,4,5,6,7]");
  puts("  aipetllm forward2fast model.gguf token1 token2 [cores]");
  puts("  aipetllm generateids model.gguf token1,token2 count[0..64] [cores]");
  puts("  aipetllm generate model.gguf \"raw text\" count[0..64] [cores]");
  puts("  aipetllm chat model.gguf \"message\" count[0..64] [cores]");
  puts("  aipetllm ask model.gguf \"message\" count[0..64] [cores]");
  puts("  aipetllm newchat | history | stop | runstatus");
  puts("  aipetllm cacheinfo | unload (persistent RAM model)");
  puts("Target: Qwen2.5-1.5B-Instruct Q4_K_M, CPU/ARM64 first.");
}

int aipetllm_dispatch(int argc, char **argv)
{
  if (argc == 2 && (strcmp(argv[1], "stop") == 0 ||
                    strcmp(argv[1], "runstatus") == 0))
    {
      return aipetllm_generation_control(strcmp(argv[1], "stop") == 0);
    }

  if (argc == 2 && (strcmp(argv[1], "newchat") == 0 ||
                    strcmp(argv[1], "history") == 0))
    {
      if (pthread_mutex_trylock(&g_chat_lock) != 0)
        {
          fputs("session busy; stop active ask first\n", stderr);
          return 1;
        }
      if (strcmp(argv[1], "newchat") == 0)
        {
          memset(g_chat_history, 0, sizeof(g_chat_history));
          g_chat_history_count = 0;
          g_chat_model[0] = '\0';
        }
      printf("llm-history: tokens=%" PRIu32 "/64 model='%s' RAM-only\n",
             g_chat_history_count, g_chat_model);
      pthread_mutex_unlock(&g_chat_lock);
      return 0;
    }

  if (argc == 2 && strcmp(argv[1], "cpucheck") == 0)
    {
      return aipetllm_cpu_checkpoint();
    }

  if (argc == 2 && strcmp(argv[1], "info") == 0)
    {
      printf("backend=CPU/ARM64 model=%s\n", MODEL_DEFAULT);
      puts("stage=Qwen2 CPU generation/ChatML; RAM model and six-core pool");
      puts("bounded prompt/history=64 tokens output=64; default cores=2..7");
      return 0;
    }

  if ((argc == 2 || argc == 3) && strcmp(argv[1], "probe") == 0)
    {
      char *end = NULL;
      unsigned long mib = argc == 3 ? strtoul(argv[2], &end, 10) : 1024;
      if (argc == 3 && (end == argv[2] || *end != '\0'))
        {
          usage();
          return 1;
        }

      return memory_probe(mib);
    }

  if (argc == 2 && strcmp(argv[1], "cxxcheck") == 0)
    {
      return aipetllm_cxx_checkpoint();
    }

  if (argc == 2 && strcmp(argv[1], "quantcheck") == 0)
    {
      return aipetllm_ggml_quant_checkpoint();
    }

  if ((argc == 2 || argc == 3) && strcmp(argv[1], "gguf") == 0)
    {
      return inspect_gguf(argc == 3 ? argv[2] : MODEL_DEFAULT);
    }

  if ((argc == 2 || argc == 3) && strcmp(argv[1], "modelcheck") == 0)
    {
      return aipetllm_model_checkpoint(argc == 3 ? argv[2] : MODEL_DEFAULT);
    }

  if ((argc == 2 || argc == 3) && strcmp(argv[1], "tokencheck") == 0)
    {
      return aipetllm_tokenizer_checkpoint(argc == 3 ? argv[2] : MODEL_DEFAULT);
    }

  if (argc >= 4 && strcmp(argv[1], "decode") == 0)
    {
      return aipetllm_decode_checkpoint(argv[2], argc - 3, &argv[3]);
    }

  if (argc == 4 && strcmp(argv[1], "encode") == 0)
    {
      return aipetllm_bpe_checkpoint(argv[2], argv[3]);
    }

  if (argc == 4 && strcmp(argv[1], "embed") == 0)
    {
      char *end;
      unsigned long token = strtoul(argv[3], &end, 10);

      if (end == argv[3] || *end != '\0' || token > UINT32_MAX)
        {
          fputs("aipetllm: embed token-id must be uint32\n", stderr);
          return 1;
        }

      return aipetllm_embedding_checkpoint(argv[2], (uint32_t)token);
    }

  if (argc == 4 && strcmp(argv[1], "qproj") == 0)
    {
      char *end;
      unsigned long token = strtoul(argv[3], &end, 10);

      if (end == argv[3] || *end != '\0' || token > UINT32_MAX)
        {
          fputs("aipetllm: qproj token-id must be uint32\n", stderr);
          return 1;
        }

      return aipetllm_q_projection_checkpoint(argv[2], (uint32_t)token);
    }

  if (argc == 5 && strcmp(argv[1], "qkv") == 0)
    {
      char *token_end;
      char *position_end;
      unsigned long token = strtoul(argv[3], &token_end, 10);
      unsigned long position = strtoul(argv[4], &position_end, 10);

      if (token_end == argv[3] || *token_end != '\0' ||
          position_end == argv[4] || *position_end != '\0' ||
          token > UINT32_MAX || position > UINT32_MAX)
        {
          fputs("aipetllm: qkv token-id/position must be uint32\n", stderr);
          return 1;
        }

      return aipetllm_qkv_checkpoint(argv[2], (uint32_t)token,
                                     (uint32_t)position);
    }

  if (argc == 5 && strcmp(argv[1], "attn2") == 0)
    {
      char *first_end;
      char *second_end;
      unsigned long first = strtoul(argv[3], &first_end, 10);
      unsigned long second = strtoul(argv[4], &second_end, 10);

      if (first_end == argv[3] || *first_end != '\0' ||
          second_end == argv[4] || *second_end != '\0' ||
          first > UINT32_MAX || second > UINT32_MAX)
        {
          fputs("aipetllm: attn2 token IDs must be uint32\n", stderr);
          return 1;
        }

      return aipetllm_attention2_checkpoint(argv[2], (uint32_t)first,
                                            (uint32_t)second);
    }

  if (argc == 5 && strcmp(argv[1], "attnblock2") == 0)
    {
      char *first_end;
      char *second_end;
      unsigned long first = strtoul(argv[3], &first_end, 10);
      unsigned long second = strtoul(argv[4], &second_end, 10);

      if (first_end == argv[3] || *first_end != '\0' ||
          second_end == argv[4] || *second_end != '\0' ||
          first > UINT32_MAX || second > UINT32_MAX)
        {
          fputs("aipetllm: attnblock2 token IDs must be uint32\n", stderr);
          return 1;
        }

      return aipetllm_attention_block2_checkpoint(argv[2],
                                                  (uint32_t)first,
                                                  (uint32_t)second);
    }

  if (argc == 5 && strcmp(argv[1], "block2") == 0)
    {
      char *first_end;
      char *second_end;
      unsigned long first = strtoul(argv[3], &first_end, 10);
      unsigned long second = strtoul(argv[4], &second_end, 10);

      if (first_end == argv[3] || *first_end != '\0' ||
          second_end == argv[4] || *second_end != '\0' ||
          first > UINT32_MAX || second > UINT32_MAX)
        {
          fputs("aipetllm: block2 token IDs must be uint32\n", stderr);
          return 1;
        }

      return aipetllm_transformer_block2_checkpoint(argv[2],
                                                     (uint32_t)first,
                                                     (uint32_t)second);
    }

  if (argc == 2 && (strcmp(argv[1], "cacheinfo") == 0 ||
                    strcmp(argv[1], "unload") == 0))
    {
      return aipetllm_cache_control(strcmp(argv[1], "unload") == 0);
    }

  if ((argc == 5 || argc == 6) &&
      (strcmp(argv[1], "generateids") == 0 ||
       strcmp(argv[1], "generate") == 0 || strcmp(argv[1], "chat") == 0 ||
       strcmp(argv[1], "ask") == 0))
    {
      return generate_ids(argc, argv);
    }

  if ((argc == 5 || argc == 6) && strcmp(argv[1], "forward2fast") == 0)
    {
      extern int aipetllm_forward2_fast_checkpoint(const char *, uint32_t,
                                                  uint32_t, unsigned int);
      char *end;
      unsigned long first = strtoul(argv[3], &end, 10);
      unsigned long second;
      unsigned int mask = 0xfc;
      if (end == argv[3] || *end != '\0' || first > UINT32_MAX ||
          argv[3][0] == '-')
        {
          return 1;
        }

      second = strtoul(argv[4], &end, 10);
      if (end == argv[4] || *end != '\0' || second > UINT32_MAX ||
          argv[4][0] == '-')
        {
          return 1;
        }

      if (argc == 6)
        {
          const char *cursor = argv[5];
          mask = 0;
          for (;;)
            {
              unsigned int bit;
              if (*cursor < '0' || *cursor > '7')
                {
                  return 1;
                }

              bit = 1u << (*cursor++ - '0');
              if (mask & bit)
                {
                  return 1;
                }

              mask |= bit;
              if (*cursor == '\0')
                {
                  break;
                }

              if (*cursor++ != ',' || *cursor == '\0')
                {
                  return 1;
                }
            }
        }

      return aipetllm_forward2_fast_checkpoint(argv[2], (uint32_t)first,
                                               (uint32_t)second, mask);
    }

  if ((argc == 4 && strcmp(argv[1], "forward1") == 0) ||
      ((argc == 4 || argc == 5) && strcmp(argv[1], "forwardfast") == 0))
    {
      char *end;
      unsigned long token = strtoul(argv[3], &end, 10);

      if (end == argv[3] || *end != '\0' || token > UINT32_MAX)
        {
          fputs("aipetllm: forward1 token-id must be uint32\n", stderr);
          return 1;
        }

      if (strcmp(argv[1], "forwardfast") == 0)
        {
          unsigned int mask = 0xfc;
          if (argc == 5)
            {
              const char *cursor = argv[4];
              mask = 0;
              do
                {
                  unsigned int cpu;
                  if (*cursor < '0' || *cursor > '7')
                    {
                      fputs("forwardfast: cores must be unique IDs 0..7, "
                            "comma separated\n", stderr);
                      return 1;
                    }

                  cpu = (unsigned int)(*cursor++ - '0');
                  if (mask & (1u << cpu))
                    {
                      fputs("forwardfast: duplicate core\n", stderr);
                      return 1;
                    }

                  mask |= 1u << cpu;
                  if (*cursor == '\0')
                    {
                      break;
                    }

                  if (*cursor++ != ',' || *cursor == '\0')
                    {
                      fputs("forwardfast: invalid core list\n", stderr);
                      return 1;
                    }
                }
              while (1);
            }

          return aipetllm_forward_fast_checkpoint(argv[2], (uint32_t)token,
                                                   mask);
        }

      return aipetllm_forward1_checkpoint(argv[2], (uint32_t)token);
    }

  if ((argc == 2 || argc == 3) && strcmp(argv[1], "pathcheck") == 0)
    {
      return path_check(argc == 3 ? argv[2] : MODEL_DEFAULT);
    }

  if ((argc == 2 || argc == 3) && strcmp(argv[1], "readcheck") == 0)
    {
      return read_check(argc == 3 ? argv[2] : MODEL_DEFAULT);
    }

  usage();
  return argc == 1 ? 0 : 1;
}

int main(int argc, char **argv)
{
  return aipetllm_dispatch(argc, argv);
}
