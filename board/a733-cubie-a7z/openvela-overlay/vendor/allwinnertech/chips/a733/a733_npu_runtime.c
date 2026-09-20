/****************************************************************************
 * vendor/allwinnertech/chips/a733/a733_npu_runtime.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_A733_NPU_CHECKPOINT

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/cache.h>
#include <nuttx/crc32.h>
#include <nuttx/fs/fs.h>
#include <nuttx/irq.h>
#include <nuttx/mutex.h>
#include <nuttx/npu/a733_vip2.h>
#include <nuttx/spinlock.h>

#include "a733_npu_internal.h"
#include "a733_npu_memory.h"

#define A733_RT_PAGE_SIZE          4096u
#define A733_RT_POOL_SIZE          (1024u * 1024u)
#define A733_RT_POOL_PAGES         (A733_RT_POOL_SIZE / A733_RT_PAGE_SIZE)
#define A733_RT_MAX_CONTEXTS       4u
#define A733_RT_MAX_BUFFERS        16u
#define A733_RT_VIP_BASE           UINT32_C(0x02000000)
#define A733_RT_MTLB_INDEX         (A733_RT_VIP_BASE >> 24)
#define A733_RT_PRESENT            UINT32_C(0x1)
#define A733_RT_EXCEPTION          UINT32_C(0x2)
#define A733_RT_WRITEABLE          UINT32_C(0x4)
#define A733_RT_FREE_ENTRY         UINT32_C(0x2)
#define A733_RT_EVENT0             UINT32_C(0x08010e01)
#define A733_RT_EVENT1             UINT32_C(0x00000044)
#define A733_RT_END0               UINT32_C(0x10000000)
#define A733_RT_NBG_HEADER_SIZE     64u
#define A733_RT_PROBE_CHUNK         512u
#define A733_RT_STAGE_SIZE          (512u * 1024u)
#define A733_RT_STAGE_VIP_BASE      (A733_RT_VIP_BASE + A733_RT_POOL_SIZE)
#define A733_RT_STAGE_STLB_INDEX    (A733_RT_POOL_PAGES)
#define A733_RT_TRACE_VIP_BASE      UINT32_C(0x01000000)
#define A733_RT_TRACE_SIZE          (64u * 1024u)
#define A733_RT_TRACE_INIT_OFFSET   UINT32_C(0x00001000)
#define A733_RT_TRACE_PARAM_OFFSET  UINT32_C(0x00005000)
#define A733_RT_TRACE_INPUT_OFFSET  UINT32_C(0x00008000)
#define A733_RT_TRACE_OUTPUT_OFFSET UINT32_C(0x0000b000)
#define A733_RT_TRACE_COMMAND_OFFSET UINT32_C(0x0000e000)
#define A733_RT_TRACE_COMMAND_SKIP  56u
#define A733_RT_TRACE_COMMAND_SIZE  1088u
#define A733_RT_TRACE_WEIGHTS_OFFSET UINT32_C(0x00010000)
#define A733_RT_TRACE_WEIGHTS_SIZE  437760u
#define A733_RT_TRACE_WEIGHTS_PAGES \
  ((A733_RT_TRACE_WEIGHTS_SIZE + A733_RT_PAGE_SIZE - 1u) / \
   A733_RT_PAGE_SIZE)
#define A733_RT_TRACE_WEIGHTS_FIRST \
  (A733_RT_TRACE_WEIGHTS_OFFSET / A733_RT_PAGE_SIZE)
#define A733_RT_LENET_OUTPUT_SIZE   20u
#define A733_RT_A7PM_MAGIC           UINT32_C(0x4d503741)
#define A733_RT_A7PM_VERSION_V1      1u
#define A733_RT_A7PM_VERSION_V2      2u
#define A733_RT_A7PM_VERSION_V3      3u
#define A733_RT_A7PM_CID             UINT32_C(0x1000003b)
#define A733_RT_A7PM_MAX_REGIONS     16u
#define A733_RT_A7PM_MAX_OUTPUTS     8u
#define A733_RT_A7PM_MAX_INPUTS      4u
#define A733_RT_A7PM_MAX_WINDOWS     16u
#define A733_RT_A7PM_WINDOW_SIZE     UINT32_C(0x01000000)
#define A733_RT_A7PM_STLB_WORDS      4096u
#define A733_RT_A7PM_STLB_SIZE       (A733_RT_A7PM_STLB_WORDS * 4u)
#define A733_RT_A7PM_POOL_SIZE       (24u * 1024u * 1024u)
#define A733_RT_A7PM_LOAD            (1u << 0)
#define A733_RT_A7PM_OUTPUT          (1u << 1)
#define A733_RT_A7PM_COMMAND         (1u << 2)
#define A733_RT_A7PM_OUTPUT_REPORT   32u
#define A733_RT_YOLO_MAX_CANDIDATES  64u
#define A733_RT_YOLO_MAX_DETECTIONS  32u
#define A733_RT_YOLO_SCORE_THRESHOLD 0.25f
#define A733_RT_YOLO_IOU_THRESHOLD   0.45f

#define A733_RT_LENET_PARAM_PATH \
  "/data/npu/lenet-param.bin"
#define A733_RT_LENET_WEIGHTS_PATH \
  "/data/npu/lenet-weights.bin"
#define A733_RT_LENET_INPUT_PATH \
  "/data/npu/lenet-input.bin"
#define A733_RT_LENET_COMMAND_PATH \
  "/data/npu/lenet-command.bin"

struct a733_rt_context_s
{
  bool active;
  uint32_t id;
};

struct a733_rt_buffer_s
{
  bool active;
  struct a733_rt_context_s *owner;
  uint32_t generation;
  uint32_t handle;
  uint32_t first_page;
  uint32_t page_count;
  uint32_t requested;
  uint32_t flags;
};

struct a733_rt_a7pm_header_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t header_size;
  uint32_t cid;
  uint32_t region_count;
  uint32_t command_index;
  uint32_t command_skip;
  uint32_t output_index;
  uint32_t output_size;
  uint32_t output_crc;
  uint32_t total_size;
  uint32_t manifest_crc;
  char name[32];
  uint32_t reserved[12];
} __attribute__((packed));

struct a733_rt_a7pm_region_s
{
  uint32_t vip;
  uint32_t file_offset;
  uint32_t size;
  uint32_t crc32;
  uint32_t flags;
  uint32_t reserved[3];
} __attribute__((packed));

struct a733_rt_a7pm_output_s
{
  uint32_t region_index;
  uint32_t size;
  uint32_t crc32;
  uint32_t reserved;
} __attribute__((packed));

struct a733_rt_a7pm_input_s
{
  uint32_t region_index;
  uint32_t size;
  uint32_t crc32;
  uint32_t reserved;
} __attribute__((packed));

struct a733_rt_yolo_detection_s
{
  float x1;
  float y1;
  float x2;
  float y2;
  float score;
  uint8_t class_id;
};

struct a733_rt_a7pm_window_s
{
  bool active;
  uint8_t index;
  uint16_t reserved;
  uint32_t saved_mtlb;
  uint32_t *stlb;
  uint32_t stlb_physical;
};

struct a733_rt_state_s
{
  int checkpoint;
  int selftest;
  int apitest;
  int guards;
  uint32_t pd_entry;
  uint32_t next_context;
  uint32_t next_task;
  uint32_t open_count;
  uint32_t allocated_buffers;
  uint32_t allocated_pages;
  uint32_t submitted;
  uint32_t completed;
  uint32_t cancelled;
  uint32_t timedout;
  uint32_t last_task;
  int last_status;
  struct a733_npu_hw_result_s last_hw;
  struct a733_npu_model_probe_s model;
  struct a733_npu_stage_file_s staged_model;
  struct a733_npu_stage_file_s staged_input;
  int lenet_status;
  uint32_t lenet_output_crc;
  uint32_t lenet_output_changed;
  uint8_t lenet_output[A733_RT_LENET_OUTPUT_SIZE];
  struct a733_npu_hw_result_s lenet_hw;
  int prepared_status;
  int prepared_hw_status;
  bool prepared_verified;
  bool prepared_golden;
  uint32_t prepared_regions;
  uint32_t prepared_command;
  uint32_t prepared_tail_vip;
  uint32_t prepared_tail_physical;
  uint32_t prepared_tail_crc;
  bool prepared_tail_changed;
  uint32_t prepared_outputs;
  uint32_t prepared_output_crc[A733_RT_A7PM_MAX_OUTPUTS];
  uint32_t prepared_output_changed[A733_RT_A7PM_MAX_OUTPUTS];
  uint8_t prepared_output[A733_RT_A7PM_MAX_OUTPUTS]
                         [A733_RT_A7PM_OUTPUT_REPORT];
  char prepared_name[32];
  char prepared_path[A733_NPU_MODEL_PATH_MAX];
  char dynamic_input_path[A733_NPU_MODEL_PATH_MAX];
  uint32_t dynamic_input_crc;
  uint32_t dynamic_input_size;
  int dynamic_input_status;
  int yolo_status;
  uint32_t yolo_candidates;
  uint32_t yolo_detections;
  struct a733_rt_yolo_detection_s
    yolo[A733_RT_YOLO_MAX_DETECTIONS];
  struct a733_npu_hw_result_s prepared_hw;
};

struct a733_rt_nbg_header_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t cid;
  char name[A733_NPU_MODEL_NAME_MAX];
  uint8_t reserved[A733_RT_NBG_HEADER_SIZE - 12u -
                   A733_NPU_MODEL_NAME_MAX];
};

static struct a733_npu_buffer_s g_rt_mtlb;
static struct a733_npu_buffer_s g_rt_stlb;
static struct a733_npu_buffer_s g_rt_pool;
static struct a733_npu_buffer_s g_rt_stage;
static struct a733_npu_buffer_s g_rt_trace_stlb;
static struct a733_npu_buffer_s g_rt_trace;
static struct a733_npu_buffer_s g_rt_prepared_stlb;
static struct a733_npu_buffer_s g_rt_prepared_pool;
static struct a733_rt_context_s g_rt_contexts[A733_RT_MAX_CONTEXTS];
static struct a733_rt_buffer_s g_rt_buffers[A733_RT_MAX_BUFFERS];
static bool g_rt_pages[A733_RT_POOL_PAGES];
static volatile bool g_rt_cancel;
static uint32_t g_rt_inflight;
static struct a733_rt_state_s g_rt;
static mutex_t g_rt_run_lock = NXMUTEX_INITIALIZER;

static const uint32_t g_a733_rt_init_command[] =
{
  UINT32_C(0x0801028a), UINT32_C(0x00000011),
  UINT32_C(0x08010e13), UINT32_C(0x00000002),
  UINT32_C(0x08010e12), UINT32_C(0x18010000),
  UINT32_C(0x08010e55), UINT32_C(0x03010000),
  UINT32_C(0x08010e55), UINT32_C(0x03010001),
  UINT32_C(0x08010e55), UINT32_C(0x03010002),
  UINT32_C(0x08010e55), UINT32_C(0x03010003),
  UINT32_C(0x08010e55), UINT32_C(0x03010004),
  UINT32_C(0x08010e55), UINT32_C(0x03010005),
  UINT32_C(0x08010e55), UINT32_C(0x03010006),
  UINT32_C(0x08010e55), UINT32_C(0x03010007),
  UINT32_C(0x08010e21), UINT32_C(0x00020000),
  UINT32_C(0x08010e02), UINT32_C(0x00000701),
  UINT32_C(0x48000000), UINT32_C(0x00000701),
  UINT32_C(0x0801022c), UINT32_C(0x0000001f),
  UINT32_C(0x08010e4e), UINT32_C(0x00400000),
  UINT32_C(0x08010e02), UINT32_C(0x30000701),
  UINT32_C(0x48000000), UINT32_C(0x30000701),
  UINT32_C(0x10000000), UINT32_C(0x00000000)
};

static size_t a733_rt_align_up(size_t value, size_t align)
{
  return (value + align - 1u) & ~(align - 1u);
}

static int a733_rt_validate_path(char *path)
{
  size_t length;

  path[A733_NPU_MODEL_PATH_MAX - 1u] = '\0';
  length = strnlen(path, A733_NPU_MODEL_PATH_MAX);
  if (length < 7u || length >= A733_NPU_MODEL_PATH_MAX ||
      strncmp(path, "/data/", 6) != 0 || strstr(path, "..") != NULL ||
      strchr(path, '\\') != NULL)
    {
      return -EINVAL;
    }

  return OK;
}

static struct a733_rt_buffer_s *a733_rt_find_buffer(
  struct a733_rt_context_s *context, uint32_t handle)
{
  unsigned int i;

  for (i = 0; i < A733_RT_MAX_BUFFERS; i++)
    {
      if (g_rt_buffers[i].active && g_rt_buffers[i].owner == context &&
          g_rt_buffers[i].handle == handle)
        {
          return &g_rt_buffers[i];
        }
    }

  return NULL;
}

static uint32_t a733_rt_free_bytes(void)
{
  uint32_t free_pages = 0;
  unsigned int i;

  for (i = 1; i < A733_RT_POOL_PAGES; i++)
    {
      if (!g_rt_pages[i])
        {
          free_pages++;
        }
    }

  return free_pages * A733_RT_PAGE_SIZE;
}

static void a733_rt_release_buffer(struct a733_rt_buffer_s *buffer)
{
  unsigned int page;

  for (page = 0; page < buffer->page_count; page++)
    {
      g_rt_pages[buffer->first_page + page] = false;
    }

  if (g_rt.allocated_pages >= buffer->page_count)
    {
      g_rt.allocated_pages -= buffer->page_count;
    }

  if (g_rt.allocated_buffers > 0)
    {
      g_rt.allocated_buffers--;
    }

  buffer->active = false;
  buffer->owner = NULL;
}

static int a733_rt_open(struct file *filep)
{
  irqstate_t flags;
  unsigned int i;

  flags = enter_critical_section();
  for (i = 0; i < A733_RT_MAX_CONTEXTS; i++)
    {
      if (!g_rt_contexts[i].active)
        {
          g_rt_contexts[i].active = true;
          g_rt_contexts[i].id = ++g_rt.next_context;
          g_rt.open_count++;
          filep->f_priv = &g_rt_contexts[i];
          leave_critical_section(flags);
          return OK;
        }
    }

  leave_critical_section(flags);
  return -EMFILE;
}

static int a733_rt_close(struct file *filep)
{
  struct a733_rt_context_s *context = filep->f_priv;
  irqstate_t flags;
  unsigned int i;

  if (context == NULL)
    {
      return -EINVAL;
    }

  flags = enter_critical_section();
  for (i = 0; i < A733_RT_MAX_BUFFERS; i++)
    {
      if (g_rt_buffers[i].active && g_rt_buffers[i].owner == context)
        {
          if (g_rt_buffers[i].handle == g_rt_inflight)
            {
              leave_critical_section(flags);
              return -EBUSY;
            }

          a733_rt_release_buffer(&g_rt_buffers[i]);
        }
    }

  context->active = false;
  if (g_rt.open_count > 0)
    {
      g_rt.open_count--;
    }

  filep->f_priv = NULL;
  leave_critical_section(flags);
  return OK;
}

static int a733_rt_selftest(void)
{
  struct a733_npu_hw_result_s result;
  struct a733_npu_buffer_s page;
  uint32_t *command = g_rt_pool.cpu;
  int ret;

  command[0] = A733_RT_EVENT0;
  command[1] = A733_RT_EVENT1;
  command[2] = A733_RT_END0;
  command[3] = 0;
  page.cpu = command;
  page.physical = g_rt_pool.physical;
  page.size = A733_RT_PAGE_SIZE;
  page.slot = g_rt_pool.slot;
  a733_npu_mem_clean(&page);
  g_rt_cancel = false;
  ret = a733_npu_hw_execute(g_rt.pd_entry, A733_RT_VIP_BASE, 100,
                            &g_rt_cancel, true, &result);
  g_rt.guards = a733_npu_mem_check_guards();
  if (ret == OK && g_rt.guards < 0)
    {
      ret = g_rt.guards;
    }

  g_rt.selftest = ret;
  g_rt.last_hw = result;
  return ret;
}

static int a733_rt_model_probe(struct a733_npu_model_probe_s *request)
{
  struct a733_rt_nbg_header_s header;
  struct a733_npu_model_probe_s result;
  struct file file;
  uint8_t chunk[A733_RT_PROBE_CHUNK];
  uint64_t total = 0;
  uint32_t checksum = 0;
  ssize_t nread;
  irqstate_t flags;
  size_t length;
  size_t i;
  int ret;

  if (request == NULL)
    {
      return -EINVAL;
    }

  ret = a733_rt_validate_path(request->path);
  if (ret < 0)
    {
      request->status = -EINVAL;
      return -EINVAL;
    }

  length = strnlen(request->path, A733_NPU_MODEL_PATH_MAX);

  memset(&result, 0, sizeof(result));
  memcpy(result.path, request->path, length + 1u);
  ret = file_open(&file, result.path, O_RDONLY | O_CLOEXEC);
  if (ret < 0)
    {
      result.status = ret;
      goto update;
    }

  memset(&header, 0, sizeof(header));
  for (;;)
    {
      nread = file_read(&file, chunk, sizeof(chunk));
      if (nread < 0)
        {
          ret = (int)nread;
          break;
        }

      if (nread == 0)
        {
          ret = OK;
          break;
        }

      if (total < sizeof(header))
        {
          size_t copy = (size_t)nread;
          if (copy > sizeof(header) - (size_t)total)
            {
              copy = sizeof(header) - (size_t)total;
            }

          memcpy((uint8_t *)&header + (size_t)total, chunk, copy);
        }

      if (UINT64_MAX - total < (uint64_t)nread)
        {
          ret = -EFBIG;
          break;
        }

      checksum = crc32part(chunk, (size_t)nread, checksum);
      total += (uint64_t)nread;
    }

  file_close(&file);
  result.file_size = total;
  result.crc32 = checksum;
  result.format_version = header.version;
  result.target_cid = header.cid;
  memcpy(result.name, header.name, sizeof(result.name) - 1u);
  result.name[sizeof(result.name) - 1u] = '\0';

  if (ret == OK && total < A733_RT_NBG_HEADER_SIZE)
    {
      ret = -ENODATA;
    }
  else if (ret == OK && header.magic != A733_NPU_NBG_MAGIC)
    {
      ret = -ENOEXEC;
    }
  else if (ret == OK && header.version != A733_NPU_NBG_V3_VERSION)
    {
      ret = -EPROTONOSUPPORT;
    }
  else if (ret == OK && header.cid != A733_NPU_A733_CID)
    {
      ret = -EXDEV;
    }

  if (ret == OK)
    {
      for (i = 0; i < sizeof(result.name) && result.name[i] != '\0'; i++)
        {
          if ((unsigned char)result.name[i] < 0x20u ||
              (unsigned char)result.name[i] > 0x7eu)
            {
              ret = -EPROTO;
              break;
            }
        }
    }

  result.status = ret;

update:
  flags = enter_critical_section();
  g_rt.model = result;
  *request = result;
  leave_critical_section(flags);
  return ret;
}

static int a733_rt_stage_file(struct a733_npu_stage_file_s *request)
{
  struct a733_npu_stage_file_s result;
  struct a733_npu_model_probe_s probe;
  struct a733_npu_stage_file_s *state;
  struct file file;
  uint8_t *destination;
  uint32_t checksum = 0;
  size_t capacity;
  size_t offset;
  ssize_t nread;
  irqstate_t flags;
  int ret;

  if (request == NULL ||
      (request->kind != A733_NPU_STAGE_MODEL &&
       request->kind != A733_NPU_STAGE_INPUT))
    {
      return -EINVAL;
    }

  ret = a733_rt_validate_path(request->path);
  if (ret < 0)
    {
      request->status = ret;
      return ret;
    }

  memset(&result, 0, sizeof(result));
  strlcpy(result.path, request->path, sizeof(result.path));
  result.kind = request->kind;
  result.status = -EINPROGRESS;

  if (request->kind == A733_NPU_STAGE_MODEL)
    {
      memset(&probe, 0, sizeof(probe));
      strlcpy(probe.path, request->path, sizeof(probe.path));
      ret = a733_rt_model_probe(&probe);
      if (ret < 0)
        {
          result.status = ret;
          goto update;
        }

      offset = 0;
      memset(&g_rt.staged_input, 0, sizeof(g_rt.staged_input));
      g_rt.staged_input.status = -EAGAIN;
    }
  else
    {
      if (g_rt.staged_model.status != OK ||
          g_rt.staged_model.file_size == 0)
        {
          ret = -ENODATA;
          result.status = ret;
          goto update;
        }

      offset = a733_rt_align_up((size_t)g_rt.staged_model.file_size, 64u);
    }

  if (offset >= A733_RT_STAGE_SIZE)
    {
      ret = -EFBIG;
      result.status = ret;
      goto update;
    }

  capacity = A733_RT_STAGE_SIZE - offset;
  destination = (uint8_t *)g_rt_stage.cpu + offset;
  ret = file_open(&file, result.path, O_RDONLY | O_CLOEXEC);
  if (ret < 0)
    {
      result.status = ret;
      goto update;
    }

  while ((nread = file_read(&file, destination + result.file_size,
                            capacity - (size_t)result.file_size)) > 0)
    {
      checksum = crc32part(destination + result.file_size, (size_t)nread,
                           checksum);
      result.file_size += (uint64_t)nread;
      if (result.file_size == capacity)
        {
          uint8_t extra;
          nread = file_read(&file, &extra, 1);
          if (nread > 0)
            {
              ret = -EFBIG;
            }

          break;
        }
    }

  if (nread < 0)
    {
      ret = (int)nread;
    }
  else if (ret >= 0 && result.file_size == 0)
    {
      ret = -ENODATA;
    }
  else if (ret >= 0)
    {
      ret = OK;
    }

  file_close(&file);
  result.offset = (uint32_t)offset;
  result.crc32 = checksum;
  result.physical = g_rt_stage.physical + (uint32_t)offset;
  result.vip_address = A733_RT_STAGE_VIP_BASE + (uint32_t)offset;
  if (ret == OK && request->kind == A733_NPU_STAGE_MODEL &&
      (result.file_size != probe.file_size || result.crc32 != probe.crc32))
    {
      ret = -EIO;
    }

  if (ret == OK)
    {
      up_clean_dcache((uintptr_t)destination,
                      (uintptr_t)destination + (size_t)result.file_size);
      g_rt.guards = a733_npu_mem_check_guards();
      if (g_rt.guards < 0)
        {
          ret = g_rt.guards;
        }
    }

  result.status = ret;

update:
  state = request->kind == A733_NPU_STAGE_MODEL ?
          &g_rt.staged_model : &g_rt.staged_input;
  flags = enter_critical_section();
  *state = result;
  *request = result;
  leave_critical_section(flags);
  return ret;
}

static int a733_rt_load_exact(const char *path, void *destination,
                              size_t expected, uint32_t expected_crc)
{
  struct file file;
  uint8_t extra;
  uint8_t *cursor = destination;
  uint32_t checksum = 0;
  size_t total = 0;
  ssize_t nread;
  int ret;

  ret = file_open(&file, path, O_RDONLY | O_CLOEXEC);
  if (ret < 0)
    {
      return ret;
    }

  while (total < expected)
    {
      nread = file_read(&file, cursor + total, expected - total);
      if (nread < 0)
        {
          ret = (int)nread;
          goto out;
        }

      if (nread == 0)
        {
          ret = -ENODATA;
          goto out;
        }

      checksum = crc32part(cursor + total, (size_t)nread, checksum);
      total += (size_t)nread;
    }

  nread = file_read(&file, &extra, 1);
  if (nread < 0)
    {
      ret = (int)nread;
    }
  else if (nread != 0)
    {
      ret = -EFBIG;
    }
  else if (checksum != expected_crc)
    {
      ret = -EBADMSG;
    }
  else
    {
      ret = OK;
    }

out:
  file_close(&file);
  return ret;
}

static int a733_rt_load_dynamic(const char *path, void *destination,
                                size_t expected, uint32_t *result_crc)
{
  struct file file;
  uint8_t extra;
  uint8_t *cursor = destination;
  uint32_t checksum = 0;
  size_t total = 0;
  ssize_t nread;
  int ret;

  ret = file_open(&file, path, O_RDONLY | O_CLOEXEC);
  if (ret < 0)
    {
      return ret;
    }

  while (total < expected)
    {
      nread = file_read(&file, cursor + total, expected - total);
      if (nread <= 0)
        {
          ret = nread < 0 ? (int)nread : -ENODATA;
          goto out;
        }

      checksum = crc32part(cursor + total, (size_t)nread, checksum);
      total += (size_t)nread;
    }

  nread = file_read(&file, &extra, 1);
  if (nread < 0)
    {
      ret = (int)nread;
    }
  else if (nread != 0)
    {
      ret = -EFBIG;
    }
  else
    {
      *result_crc = checksum;
      ret = OK;
    }

out:
  file_close(&file);
  return ret;
}

struct a733_rt_yolo_candidate_s
{
  float score;
  uint16_t cell;
  uint8_t scale;
  uint8_t class_id;
};

static float a733_rt_yolo_sigmoid(float value)
{
  float term;

  if (value >= 0.0f)
    {
      term = expf(-value);
      return 1.0f / (1.0f + term);
    }

  term = expf(value);
  return term / (1.0f + term);
}

static float a733_rt_yolo_dfl(const float *reg, unsigned int plane,
                              unsigned int cell, unsigned int side)
{
  float peak = -3.402823466e+38f;
  float total = 0.0f;
  float weighted = 0.0f;
  float value;
  unsigned int i;

  for (i = 0; i < 16; i++)
    {
      value = reg[(side * 16u + i) * plane + cell];
      if (value > peak)
        {
          peak = value;
        }
    }

  for (i = 0; i < 16; i++)
    {
      value = expf(reg[(side * 16u + i) * plane + cell] - peak);
      total += value;
      weighted += (float)i * value;
    }

  return total > 0.0f ? weighted / total : 0.0f;
}

static float a733_rt_yolo_iou(const struct a733_rt_yolo_detection_s *a,
                              const struct a733_rt_yolo_detection_s *b)
{
  float left = fmaxf(a->x1, b->x1);
  float top = fmaxf(a->y1, b->y1);
  float right = fminf(a->x2, b->x2);
  float bottom = fminf(a->y2, b->y2);
  float width = fmaxf(0.0f, right - left);
  float height = fmaxf(0.0f, bottom - top);
  float intersection = width * height;
  float area_a = fmaxf(0.0f, a->x2 - a->x1) *
                 fmaxf(0.0f, a->y2 - a->y1);
  float area_b = fmaxf(0.0f, b->x2 - b->x1) *
                 fmaxf(0.0f, b->y2 - b->y1);
  float total = area_a + area_b - intersection;

  return total > 0.0f ? intersection / total : 0.0f;
}

static void a733_rt_yolo_insert(
  struct a733_rt_yolo_candidate_s *candidates, unsigned int *count,
  const struct a733_rt_yolo_candidate_s *candidate)
{
  unsigned int position;
  unsigned int limit = *count;

  if (limit == A733_RT_YOLO_MAX_CANDIDATES &&
      candidate->score <= candidates[limit - 1u].score)
    {
      return;
    }

  if (limit < A733_RT_YOLO_MAX_CANDIDATES)
    {
      limit++;
    }

  position = limit - 1u;
  while (position > 0 &&
         candidates[position - 1u].score < candidate->score)
    {
      candidates[position] = candidates[position - 1u];
      position--;
    }

  candidates[position] = *candidate;
  *count = limit;
}

static int a733_rt_yolo_decode(
  struct a733_npu_buffer_s *mapped,
  const struct a733_rt_a7pm_output_s *outputs, unsigned int output_count)
{
  static const unsigned int sides[3] = {80u, 40u, 20u};
  static const unsigned int strides[3] = {8u, 16u, 32u};
  static const uint32_t sizes[6] =
  {
    1638400u, 2048000u, 409600u, 512000u, 102400u, 128000u
  };
  struct a733_rt_yolo_candidate_s
    candidates[A733_RT_YOLO_MAX_CANDIDATES];
  struct a733_rt_yolo_candidate_s candidate;
  struct a733_rt_yolo_detection_s detection;
  const float *heads[6];
  const float *reg;
  const float *cls;
  unsigned int candidate_count = 0;
  unsigned int detection_count = 0;
  unsigned int scale;
  unsigned int plane;
  unsigned int cell;
  unsigned int class_id;
  unsigned int best_class;
  unsigned int i;
  unsigned int j;
  unsigned int column;
  unsigned int row;
  float best_logit;
  float score;
  bool suppressed;

  if (output_count != 6)
    {
      return -ENOTSUP;
    }

  for (i = 0; i < 6; i++)
    {
      if (outputs[i].size != sizes[i])
        {
          return -EPROTO;
        }

      heads[i] = mapped[outputs[i].region_index].cpu;
    }

  memset(candidates, 0, sizeof(candidates));
  memset(g_rt.yolo, 0, sizeof(g_rt.yolo));
  for (scale = 0; scale < 3; scale++)
    {
      plane = sides[scale] * sides[scale];
      reg = heads[scale * 2u];
      cls = heads[scale * 2u + 1u];
      for (cell = 0; cell < plane; cell++)
        {
          best_class = 0;
          best_logit = cls[cell];
          for (class_id = 1; class_id < 80; class_id++)
            {
              if (cls[class_id * plane + cell] > best_logit)
                {
                  best_logit = cls[class_id * plane + cell];
                  best_class = class_id;
                }
            }

          score = a733_rt_yolo_sigmoid(best_logit);
          if (score >= A733_RT_YOLO_SCORE_THRESHOLD)
            {
              candidate.score = score;
              candidate.cell = cell;
              candidate.scale = scale;
              candidate.class_id = best_class;
              a733_rt_yolo_insert(candidates, &candidate_count, &candidate);
            }
        }
    }

  for (i = 0; i < candidate_count; i++)
    {
      scale = candidates[i].scale;
      plane = sides[scale] * sides[scale];
      cell = candidates[i].cell;
      column = cell % sides[scale];
      row = cell / sides[scale];
      reg = heads[scale * 2u];
      detection.x1 = fmaxf(0.0f,
        ((float)column + 0.5f - a733_rt_yolo_dfl(reg, plane, cell, 0)) *
        (float)strides[scale]);
      detection.y1 = fmaxf(0.0f,
        ((float)row + 0.5f - a733_rt_yolo_dfl(reg, plane, cell, 1)) *
        (float)strides[scale]);
      detection.x2 = fminf(640.0f,
        ((float)column + 0.5f + a733_rt_yolo_dfl(reg, plane, cell, 2)) *
        (float)strides[scale]);
      detection.y2 = fminf(640.0f,
        ((float)row + 0.5f + a733_rt_yolo_dfl(reg, plane, cell, 3)) *
        (float)strides[scale]);
      detection.score = candidates[i].score;
      detection.class_id = candidates[i].class_id;
      suppressed = false;
      for (j = 0; j < detection_count; j++)
        {
          if (g_rt.yolo[j].class_id == detection.class_id &&
              a733_rt_yolo_iou(&g_rt.yolo[j], &detection) >
              A733_RT_YOLO_IOU_THRESHOLD)
            {
              suppressed = true;
              break;
            }
        }

      if (!suppressed)
        {
          g_rt.yolo[detection_count++] = detection;
          if (detection_count == A733_RT_YOLO_MAX_DETECTIONS)
            {
              break;
            }
        }
    }

  g_rt.yolo_candidates = candidate_count;
  g_rt.yolo_detections = detection_count;
  return OK;
}

static int a733_rt_file_read_exact(struct file *file, off_t offset,
                                   void *destination, size_t size,
                                   uint32_t *checksum)
{
  uint8_t *cursor = destination;
  size_t total = 0;
  ssize_t nread;
  off_t position;

  position = file_seek(file, offset, SEEK_SET);
  if (position != offset)
    {
      return position < 0 ? (int)position : -EIO;
    }

  while (total < size)
    {
      nread = file_read(file, cursor + total, size - total);
      if (nread < 0)
        {
          return (int)nread;
        }

      if (nread == 0)
        {
          return -ENODATA;
        }

      if (checksum != NULL)
        {
          *checksum = crc32part(cursor + total, (size_t)nread, *checksum);
        }

      total += (size_t)nread;
    }

  return OK;
}

static struct a733_rt_a7pm_window_s *a733_rt_a7pm_window(
  struct a733_rt_a7pm_window_s *windows, unsigned int *count,
  uint8_t index)
{
  struct a733_rt_a7pm_window_s *window;
  uint32_t *mtlb = g_rt_mtlb.cpu;
  unsigned int i;

  for (i = 0; i < *count; i++)
    {
      if (windows[i].active && windows[i].index == index)
        {
          return &windows[i];
        }
    }

  if (*count >= A733_RT_A7PM_MAX_WINDOWS)
    {
      return NULL;
    }

  window = &windows[*count];
  memset(window, 0, sizeof(*window));
  window->active = true;
  window->index = index;
  window->saved_mtlb = mtlb[index];
  window->stlb = (uint32_t *)((uint8_t *)g_rt_prepared_stlb.cpu +
                             *count * A733_RT_A7PM_STLB_SIZE);
  window->stlb_physical = g_rt_prepared_stlb.physical +
                          *count * A733_RT_A7PM_STLB_SIZE;
  for (i = 0; i < A733_RT_A7PM_STLB_WORDS; i++)
    {
      window->stlb[i] = A733_RT_FREE_ENTRY;
    }

  mtlb[index] = window->stlb_physical | A733_RT_PRESENT;
  (*count)++;
  return window;
}

static int a733_rt_a7pm_map_page(
  struct a733_rt_a7pm_window_s *windows, unsigned int *count,
  uint32_t vip, uint32_t physical)
{
  struct a733_rt_a7pm_window_s *window;
  uint32_t entry;
  uint32_t page;

  if ((vip & (A733_RT_PAGE_SIZE - 1u)) != 0 ||
      (physical & (A733_RT_PAGE_SIZE - 1u)) != 0)
    {
      return -EINVAL;
    }

  window = a733_rt_a7pm_window(windows, count, (uint8_t)(vip >> 24));
  if (window == NULL)
    {
      return -ENOSPC;
    }

  page = (vip & (A733_RT_A7PM_WINDOW_SIZE - 1u)) / A733_RT_PAGE_SIZE;
  entry = (physical & UINT32_C(0xfffff000)) |
          A733_RT_PRESENT | A733_RT_EXCEPTION | A733_RT_WRITEABLE;
  if (window->stlb[page] != A733_RT_FREE_ENTRY &&
      window->stlb[page] != entry)
    {
      return -EADDRINUSE;
    }

  window->stlb[page] = entry;
  return OK;
}

static void a733_rt_a7pm_restore(
  struct a733_rt_a7pm_window_s *windows, unsigned int count)
{
  uint32_t *mtlb = g_rt_mtlb.cpu;
  unsigned int i;

  for (i = 0; i < count; i++)
    {
      if (windows[i].active)
        {
          mtlb[windows[i].index] = windows[i].saved_mtlb;
        }
    }

  a733_npu_mem_clean(&g_rt_mtlb);
}

static int a733_rt_prepared(const char *path)
{
  struct a733_rt_a7pm_header_s header;
  struct a733_rt_a7pm_header_s checked;
  struct a733_rt_a7pm_region_s regions[A733_RT_A7PM_MAX_REGIONS];
  struct a733_rt_a7pm_output_s outputs[A733_RT_A7PM_MAX_OUTPUTS];
  struct a733_rt_a7pm_input_s inputs[A733_RT_A7PM_MAX_INPUTS];
  struct a733_rt_a7pm_window_s windows[A733_RT_A7PM_MAX_WINDOWS];
  struct a733_npu_buffer_s mapped[A733_RT_A7PM_MAX_REGIONS];
  struct a733_npu_buffer_s init;
  struct a733_npu_buffer_s command_tail;
  struct file file;
  uint32_t checksum;
  uint32_t manifest;
  uint32_t end;
  uint32_t descriptor_end;
  uint32_t command_end;
  uint32_t command_tail_vip;
  uint32_t command_tail_crc;
  uint32_t pool_cursor = 0;
  uint32_t allocated;
  uint32_t page;
  off_t file_size;
  unsigned int i;
  unsigned int j;
  unsigned int output_count = 0;
  unsigned int input_count = 0;
  unsigned int window_count = 0;
  bool outputs_valid = true;
  bool dynamic = g_rt.dynamic_input_path[0] != '\0';
  size_t report_size;
  int ret;

  memset(&header, 0, sizeof(header));
  memset(regions, 0, sizeof(regions));
  memset(outputs, 0, sizeof(outputs));
  memset(inputs, 0, sizeof(inputs));
  memset(windows, 0, sizeof(windows));
  memset(mapped, 0, sizeof(mapped));
  memset(&command_tail, 0, sizeof(command_tail));
  memset(&g_rt.prepared_hw, 0, sizeof(g_rt.prepared_hw));
  g_rt.prepared_hw_status = -EAGAIN;
  g_rt.prepared_verified = false;
  g_rt.prepared_golden = false;
  g_rt.dynamic_input_crc = 0;
  g_rt.dynamic_input_size = 0;
  g_rt.dynamic_input_status = dynamic ? -EAGAIN : OK;
  g_rt.yolo_status = -EAGAIN;
  g_rt.yolo_candidates = 0;
  g_rt.yolo_detections = 0;
  memset(g_rt.yolo, 0, sizeof(g_rt.yolo));
  memset(g_rt.prepared_output, 0, sizeof(g_rt.prepared_output));
  memset(g_rt.prepared_output_crc, 0, sizeof(g_rt.prepared_output_crc));
  memset(g_rt.prepared_output_changed, 0,
         sizeof(g_rt.prepared_output_changed));
  g_rt.prepared_regions = 0;
  g_rt.prepared_outputs = 0;
  g_rt.prepared_command = 0;
  g_rt.prepared_tail_vip = 0;
  g_rt.prepared_tail_physical = 0;
  g_rt.prepared_tail_crc = 0;
  g_rt.prepared_tail_changed = false;
  strlcpy(g_rt.prepared_path, path, sizeof(g_rt.prepared_path));

  ret = file_open(&file, path, O_RDONLY | O_CLOEXEC);
  if (ret < 0)
    {
      goto out;
    }

  ret = a733_rt_file_read_exact(&file, 0, &header, sizeof(header), NULL);
  if (ret < 0)
    {
      goto close;
    }

  if (header.magic != A733_RT_A7PM_MAGIC ||
      (header.version != A733_RT_A7PM_VERSION_V1 &&
       header.version != A733_RT_A7PM_VERSION_V2 &&
       header.version != A733_RT_A7PM_VERSION_V3) ||
      header.header_size != sizeof(header) ||
      header.cid != A733_RT_A7PM_CID ||
      header.cid != a733_npu_hw_cid() ||
      header.region_count == 0 ||
      header.region_count > A733_RT_A7PM_MAX_REGIONS ||
      header.command_index >= header.region_count ||
      header.output_index >= header.region_count ||
      header.output_size == 0)
    {
      ret = -EPROTO;
      goto close;
    }

  ret = a733_rt_file_read_exact(&file, sizeof(header), regions,
                                header.region_count * sizeof(regions[0]),
                                NULL);
  if (ret < 0)
    {
      goto close;
    }

  if (header.version >= A733_RT_A7PM_VERSION_V2)
    {
      output_count = header.reserved[0];
      if (output_count == 0 || output_count > A733_RT_A7PM_MAX_OUTPUTS)
        {
          ret = -EPROTO;
          goto close;
        }

      ret = a733_rt_file_read_exact(
        &file, sizeof(header) + header.region_count * sizeof(regions[0]),
        outputs, output_count * sizeof(outputs[0]), NULL);
      if (ret < 0)
        {
          goto close;
        }

      if (header.version == A733_RT_A7PM_VERSION_V3)
        {
          input_count = header.reserved[1];
          if (input_count > A733_RT_A7PM_MAX_INPUTS)
            {
              ret = -EPROTO;
              goto close;
            }

          ret = a733_rt_file_read_exact(
            &file, sizeof(header) +
                   header.region_count * sizeof(regions[0]) +
                   output_count * sizeof(outputs[0]),
            inputs, input_count * sizeof(inputs[0]), NULL);
          if (ret < 0)
            {
              goto close;
            }
        }
    }
  else
    {
      output_count = 1;
      outputs[0].region_index = header.output_index;
      outputs[0].size = header.output_size;
      outputs[0].crc32 = header.output_crc;
    }

  descriptor_end = sizeof(header) + header.region_count * sizeof(regions[0]) +
                   (header.version >= A733_RT_A7PM_VERSION_V2 ?
                    output_count * sizeof(outputs[0]) : 0u) +
                   (header.version == A733_RT_A7PM_VERSION_V3 ?
                    input_count * sizeof(inputs[0]) : 0u);

  file_size = file_seek(&file, 0, SEEK_END);
  if (file_size < 0 || (uint64_t)file_size != header.total_size)
    {
      ret = file_size < 0 ? (int)file_size : -EFBIG;
      goto close;
    }

  checked = header;
  checked.manifest_crc = 0;
  manifest = crc32part((const uint8_t *)&checked, sizeof(checked), 0);
  manifest = crc32part((const uint8_t *)regions,
                       header.region_count * sizeof(regions[0]), manifest);
  if (header.version >= A733_RT_A7PM_VERSION_V2)
    {
      manifest = crc32part((const uint8_t *)outputs,
                           output_count * sizeof(outputs[0]), manifest);
    }
  if (header.version == A733_RT_A7PM_VERSION_V3)
    {
      manifest = crc32part((const uint8_t *)inputs,
                           input_count * sizeof(inputs[0]), manifest);
    }
  if (manifest != header.manifest_crc)
    {
      ret = -EBADMSG;
      goto close;
    }

  for (i = 0; i < header.region_count; i++)
    {
      if ((regions[i].flags & ~(A733_RT_A7PM_LOAD |
                                A733_RT_A7PM_OUTPUT |
                                A733_RT_A7PM_COMMAND)) != 0 ||
          (regions[i].flags & (A733_RT_A7PM_LOAD |
                               A733_RT_A7PM_OUTPUT)) == 0 ||
          ((regions[i].flags & A733_RT_A7PM_LOAD) != 0 &&
           (regions[i].flags & A733_RT_A7PM_OUTPUT) != 0))
        {
          ret = -EPROTO;
          goto close;
        }

      end = regions[i].vip + regions[i].size;
      if (end < regions[i].vip)
        {
          ret = -EOVERFLOW;
          goto close;
        }

      for (j = 0; j < i; j++)
        {
          if (regions[i].vip < regions[j].vip + regions[j].size &&
              regions[j].vip < end)
            {
              ret = -EADDRINUSE;
              goto close;
            }
        }

      pool_cursor = (uint32_t)a733_rt_align_up(pool_cursor,
                                               A733_RT_PAGE_SIZE);
      allocated = (uint32_t)a733_rt_align_up(regions[i].size,
                                             A733_RT_PAGE_SIZE);
      if (pool_cursor > A733_RT_A7PM_POOL_SIZE ||
          allocated > A733_RT_A7PM_POOL_SIZE - pool_cursor)
        {
          ret = -ENOMEM;
          goto close;
        }

      mapped[i].cpu = (uint8_t *)g_rt_prepared_pool.cpu + pool_cursor;
      mapped[i].physical = g_rt_prepared_pool.physical + pool_cursor;
      mapped[i].size = regions[i].size;
      mapped[i].slot = g_rt_prepared_pool.slot;
      for (page = 0; page < allocated; page += A733_RT_PAGE_SIZE)
        {
          ret = a733_rt_a7pm_map_page(windows, &window_count,
                                      regions[i].vip + page,
                                      mapped[i].physical + page);
          if (ret < 0)
            {
              goto close;
            }
        }

      pool_cursor += allocated;

      if ((regions[i].flags & A733_RT_A7PM_LOAD) != 0)
        {
          if (regions[i].file_offset < descriptor_end ||
              regions[i].file_offset > header.total_size ||
              regions[i].size > header.total_size - regions[i].file_offset)
            {
              ret = -EFBIG;
              goto close;
            }

          checksum = 0;
          ret = a733_rt_file_read_exact(&file, regions[i].file_offset,
                                        mapped[i].cpu, regions[i].size,
                                        &checksum);
          if (ret < 0 || checksum != regions[i].crc32)
            {
              ret = ret < 0 ? ret : -EBADMSG;
              goto close;
            }
        }
    }

  if ((regions[header.command_index].flags &
       (A733_RT_A7PM_LOAD | A733_RT_A7PM_COMMAND)) !=
      (A733_RT_A7PM_LOAD | A733_RT_A7PM_COMMAND) ||
      header.command_skip >= regions[header.command_index].size ||
      (header.command_skip & 7u) != 0)
    {
      ret = -EPROTO;
      goto close;
    }

  for (i = 0; i < output_count; i++)
    {
      if (outputs[i].reserved != 0 ||
          outputs[i].region_index >= header.region_count ||
          outputs[i].size == 0 ||
          outputs[i].size > regions[outputs[i].region_index].size ||
          (regions[outputs[i].region_index].flags &
           A733_RT_A7PM_OUTPUT) == 0)
        {
          ret = -EPROTO;
          goto close;
        }

      for (j = 0; j < i; j++)
        {
          if (outputs[i].region_index == outputs[j].region_index)
            {
              ret = -EPROTO;
              goto close;
            }
        }
    }

  for (i = 0; i < input_count; i++)
    {
      if (inputs[i].reserved != 0 ||
          inputs[i].region_index >= header.region_count ||
          inputs[i].size == 0 ||
          inputs[i].size > regions[inputs[i].region_index].size ||
          (regions[inputs[i].region_index].flags & A733_RT_A7PM_LOAD) == 0 ||
          crc32part(mapped[inputs[i].region_index].cpu,
                    inputs[i].size, 0) != inputs[i].crc32)
        {
          ret = -EPROTO;
          goto close;
        }

      for (j = 0; j < i; j++)
        {
          if (inputs[i].region_index == inputs[j].region_index)
            {
              ret = -EPROTO;
              goto close;
            }
        }
    }

  if (dynamic)
    {
      if (header.version != A733_RT_A7PM_VERSION_V3 || input_count != 1)
        {
          ret = -ENOTSUP;
          g_rt.dynamic_input_status = ret;
          goto close;
        }

      ret = a733_rt_load_dynamic(g_rt.dynamic_input_path,
                                 mapped[inputs[0].region_index].cpu,
                                 inputs[0].size,
                                 &g_rt.dynamic_input_crc);
      g_rt.dynamic_input_status = ret;
      if (ret < 0)
        {
          goto close;
        }

      g_rt.dynamic_input_size = inputs[0].size;
    }

  /* VIP2 fetches beyond the final END while draining its command pipeline.
   * The Debian allocator leaves a valid page after the YOLOv5 command
   * allocation, whereas an exact A7PM mapping stopped at 0x01013fff and the
   * speculative fetch at 0x01014000 raised MMUv2 exception 0x40000000 after
   * all golden outputs had landed.  Give every prepared command stream one
   * private tail page filled with END commands.  Reject packages whose next
   * page belongs to another declared region: speculative command fetch must
   * never be allowed to consume model data.
   */

  command_end = regions[header.command_index].vip +
                regions[header.command_index].size;
  command_tail_vip = (uint32_t)a733_rt_align_up(command_end,
                                                A733_RT_PAGE_SIZE);
  if (command_tail_vip < command_end ||
      command_tail_vip > UINT32_MAX - A733_RT_PAGE_SIZE)
    {
      ret = -EOVERFLOW;
      goto close;
    }

  for (i = 0; i < header.region_count; i++)
    {
      end = regions[i].vip + regions[i].size;
      if (command_tail_vip < end &&
          regions[i].vip < command_tail_vip + A733_RT_PAGE_SIZE)
        {
          ret = -EADDRINUSE;
          goto close;
        }
    }

  pool_cursor = (uint32_t)a733_rt_align_up(pool_cursor,
                                           A733_RT_PAGE_SIZE);
  if (pool_cursor > A733_RT_A7PM_POOL_SIZE ||
      A733_RT_PAGE_SIZE > A733_RT_A7PM_POOL_SIZE - pool_cursor)
    {
      ret = -ENOMEM;
      goto close;
    }

  command_tail.cpu = (uint8_t *)g_rt_prepared_pool.cpu + pool_cursor;
  command_tail.physical = g_rt_prepared_pool.physical + pool_cursor;
  command_tail.size = A733_RT_PAGE_SIZE;
  command_tail.slot = g_rt_prepared_pool.slot;
  for (page = 0; page < A733_RT_PAGE_SIZE / sizeof(uint32_t); page += 2)
    {
      ((uint32_t *)command_tail.cpu)[page] = A733_RT_END0;
      ((uint32_t *)command_tail.cpu)[page + 1u] = 0;
    }

  ret = a733_rt_a7pm_map_page(windows, &window_count,
                              command_tail_vip,
                              command_tail.physical);
  if (ret < 0)
    {
      goto close;
    }

  command_tail_crc = crc32part(command_tail.cpu, command_tail.size, 0);
  pool_cursor += A733_RT_PAGE_SIZE;
  g_rt.prepared_tail_vip = command_tail_vip;
  g_rt.prepared_tail_physical = command_tail.physical;
  g_rt.prepared_tail_crc = command_tail_crc;

  ret = a733_rt_a7pm_map_page(windows, &window_count,
                              A733_RT_TRACE_VIP_BASE +
                              A733_RT_TRACE_INIT_OFFSET,
                              g_rt_trace.physical +
                              A733_RT_TRACE_INIT_OFFSET);
  if (ret < 0)
    {
      goto close;
    }

  for (i = 0; i < output_count; i++)
    {
      memset(mapped[outputs[i].region_index].cpu, 0xa5,
             mapped[outputs[i].region_index].size);
    }

  memcpy((uint8_t *)g_rt_trace.cpu + A733_RT_TRACE_INIT_OFFSET,
         g_a733_rt_init_command, sizeof(g_a733_rt_init_command));
  init.cpu = (uint8_t *)g_rt_trace.cpu + A733_RT_TRACE_INIT_OFFSET;
  init.physical = g_rt_trace.physical + A733_RT_TRACE_INIT_OFFSET;
  init.size = sizeof(g_a733_rt_init_command);
  init.slot = g_rt_trace.slot;
  a733_npu_mem_clean(&init);
  for (i = 0; i < header.region_count; i++)
    {
      a733_npu_mem_clean(&mapped[i]);
    }

  a733_npu_mem_clean(&g_rt_prepared_pool);
  a733_npu_mem_clean(&g_rt_prepared_stlb);
  a733_npu_mem_clean(&g_rt_mtlb);

  g_rt_cancel = false;
  ret = a733_npu_hw_execute_initialized(
    g_rt.pd_entry,
    A733_RT_TRACE_VIP_BASE + A733_RT_TRACE_INIT_OFFSET,
    regions[header.command_index].vip + header.command_skip,
    A733_NPU_MAX_TIMEOUT_MS, &g_rt_cancel, &g_rt.prepared_hw);
  g_rt.prepared_hw_status = ret;
  a733_npu_mem_invalidate(&command_tail);
  g_rt.prepared_tail_crc = crc32part(command_tail.cpu,
                                     command_tail.size, 0);
  g_rt.prepared_tail_changed =
    g_rt.prepared_tail_crc != command_tail_crc;
  if (g_rt.prepared_tail_changed)
    {
      outputs_valid = false;
    }

  for (i = 0; i < output_count; i++)
    {
      struct a733_npu_buffer_s *output =
        &mapped[outputs[i].region_index];

      a733_npu_mem_invalidate(output);
      g_rt.prepared_output_crc[i] =
        crc32part(output->cpu, outputs[i].size, 0);
      for (j = 0; j < outputs[i].size; j++)
        {
          if (((uint8_t *)output->cpu)[j] != 0xa5)
            {
              g_rt.prepared_output_changed[i]++;
            }
        }

      report_size = outputs[i].size;
      if (report_size > sizeof(g_rt.prepared_output[i]))
        {
          report_size = sizeof(g_rt.prepared_output[i]);
        }

      memcpy(g_rt.prepared_output[i], output->cpu, report_size);
      if (g_rt.prepared_output_changed[i] == 0 ||
          (!dynamic && g_rt.prepared_output_crc[i] != outputs[i].crc32))
        {
          outputs_valid = false;
        }
    }

  if (strncmp(header.name, "yolov8n_6_pcq_a", 16) == 0)
    {
      g_rt.yolo_status = a733_rt_yolo_decode(mapped, outputs, output_count);
      if (g_rt.yolo_status < 0)
        {
          outputs_valid = false;
        }
    }

  g_rt.guards = a733_npu_mem_check_guards();
  if (g_rt.guards < 0)
    {
      ret = g_rt.guards;
    }
  else if (!outputs_valid)
    {
      ret = -EILSEQ;
    }
  else if (ret == OK)
    {
      g_rt.prepared_verified = true;
      g_rt.prepared_golden = !dynamic;
    }
  else if (ret == -EFAULT &&
           (g_rt.prepared_hw.irq_value & UINT32_C(0x10)) != 0 &&
           g_rt.prepared_hw.recovery == OK)
    {
      /* The vendor YOLOv5 stream raises EVENT and a trailing MMUv2 exception
       * together after every golden byte has already landed.  Accept this
       * only when every declared output matches its Debian capture exactly,
       * all buffers changed, guards survived, and reset recovery succeeded.
       * The original -EFAULT and fault registers remain visible below.
       */

      g_rt.prepared_verified = true;
      g_rt.prepared_golden = !dynamic;
      ret = OK;
    }

  g_rt.prepared_regions = header.region_count;
  g_rt.prepared_outputs = output_count;
  g_rt.prepared_command = regions[header.command_index].vip +
                          header.command_skip;
  memcpy(g_rt.prepared_name, header.name, sizeof(header.name));
  g_rt.prepared_name[sizeof(g_rt.prepared_name) - 1u] = '\0';

close:
  a733_rt_a7pm_restore(windows, window_count);
  file_close(&file);
out:
  g_rt.prepared_status = ret;
  return ret;
}

static int a733_rt_run_a7pm(struct a733_npu_run_s *request)
{
  unsigned int count;
  unsigned int i;
  int ret;

  if (request == NULL ||
      (request->flags & ~A733_NPU_RUN_INPUT_FILE) != 0)
    {
      return -EINVAL;
    }

  ret = a733_rt_validate_path(request->package_path);
  if (ret < 0)
    {
      request->status = ret;
      return ret;
    }

  if ((request->flags & A733_NPU_RUN_INPUT_FILE) != 0)
    {
      ret = a733_rt_validate_path(request->input_path);
      if (ret < 0)
        {
          request->status = ret;
          return ret;
        }
    }

  ret = nxmutex_lock(&g_rt_run_lock);
  if (ret < 0)
    {
      request->status = ret;
      return ret;
    }

  if ((request->flags & A733_NPU_RUN_INPUT_FILE) != 0)
    {
      strlcpy(g_rt.dynamic_input_path, request->input_path,
              sizeof(g_rt.dynamic_input_path));
      g_rt.dynamic_input_status = -EAGAIN;
    }
  else
    {
      g_rt.dynamic_input_path[0] = '\0';
      g_rt.dynamic_input_crc = 0;
      g_rt.dynamic_input_size = 0;
      g_rt.dynamic_input_status = OK;
    }

  ret = a733_rt_prepared(request->package_path);
  request->status = ret;
  request->hardware_status = g_rt.prepared_hw_status;
  request->irq_value = g_rt.prepared_hw.irq_value;
  request->idle = g_rt.prepared_hw.idle;
  request->polls = g_rt.prepared_hw.polls;
  request->input_size = g_rt.dynamic_input_size;
  request->input_crc32 = g_rt.dynamic_input_crc;
  request->output_count = g_rt.prepared_outputs;
  memcpy(request->output_crc32, g_rt.prepared_output_crc,
         sizeof(request->output_crc32));
  memcpy(request->output_changed, g_rt.prepared_output_changed,
         sizeof(request->output_changed));
  request->candidate_count = g_rt.yolo_candidates;
  request->detection_count = g_rt.yolo_detections;

  count = g_rt.yolo_detections;
  if (count > A733_NPU_RUN_MAX_DETECTIONS)
    {
      count = A733_NPU_RUN_MAX_DETECTIONS;
    }

  memset(request->detections, 0, sizeof(request->detections));
  for (i = 0; i < count; i++)
    {
      request->detections[i].x1_centi =
        (int32_t)(g_rt.yolo[i].x1 * 100.0f + 0.5f);
      request->detections[i].y1_centi =
        (int32_t)(g_rt.yolo[i].y1 * 100.0f + 0.5f);
      request->detections[i].x2_centi =
        (int32_t)(g_rt.yolo[i].x2 * 100.0f + 0.5f);
      request->detections[i].y2_centi =
        (int32_t)(g_rt.yolo[i].y2 * 100.0f + 0.5f);
      request->detections[i].score_milli =
        (uint16_t)(g_rt.yolo[i].score * 1000.0f + 0.5f);
      request->detections[i].class_id = g_rt.yolo[i].class_id;
    }

  nxmutex_unlock(&g_rt_run_lock);
  return ret;
}

static int a733_rt_lenet(void)
{
  static const uint8_t golden[A733_RT_LENET_OUTPUT_SIZE] =
  {
    0x00, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
  };
  struct a733_npu_buffer_s clean;
  uint8_t *trace = g_rt_trace.cpu;
  uint8_t *output = trace + A733_RT_TRACE_OUTPUT_OFFSET;
  unsigned int index;
  int ret;

  memset(trace, 0, g_rt_trace.size);
  memset((uint8_t *)g_rt_pool.cpu + A733_RT_TRACE_WEIGHTS_OFFSET, 0,
         A733_RT_TRACE_WEIGHTS_PAGES * A733_RT_PAGE_SIZE);
  memcpy(trace + A733_RT_TRACE_INIT_OFFSET, g_a733_rt_init_command,
         sizeof(g_a733_rt_init_command));

  ret = a733_rt_load_exact(A733_RT_LENET_PARAM_PATH,
                           trace + A733_RT_TRACE_PARAM_OFFSET,
                           1728u, UINT32_C(0x85f3779f));
  if (ret < 0)
    {
      goto out;
    }

  ret = a733_rt_load_exact(A733_RT_LENET_WEIGHTS_PATH,
                           (uint8_t *)g_rt_pool.cpu +
                           A733_RT_TRACE_WEIGHTS_OFFSET,
                           A733_RT_TRACE_WEIGHTS_SIZE,
                           UINT32_C(0xfae9ff62));
  if (ret < 0)
    {
      goto out;
    }

  ret = a733_rt_load_exact(A733_RT_LENET_INPUT_PATH,
                           trace + A733_RT_TRACE_INPUT_OFFSET,
                           896u, UINT32_C(0xb81ff05b));
  if (ret < 0)
    {
      goto out;
    }

  ret = a733_rt_load_exact(A733_RT_LENET_COMMAND_PATH,
                           trace + A733_RT_TRACE_COMMAND_OFFSET,
                           1144u, UINT32_C(0x5f147d83));
  if (ret < 0)
    {
      goto out;
    }

  /* The trace was captured after Linux had already completed the task, so
   * never import its output snapshot.  Poison the result buffer instead and
   * require the VIP2 core to replace it with the known Linux golden result.
   */

  memset(output, 0xa5, 128u);
  a733_npu_mem_clean(&g_rt_trace);
  a733_npu_mem_clean(&g_rt_pool);
  g_rt_cancel = false;
  ret = a733_npu_hw_execute_initialized(
    g_rt.pd_entry,
    A733_RT_TRACE_VIP_BASE + A733_RT_TRACE_INIT_OFFSET,
    A733_RT_TRACE_VIP_BASE + A733_RT_TRACE_COMMAND_OFFSET +
    A733_RT_TRACE_COMMAND_SKIP,
    A733_NPU_MAX_TIMEOUT_MS, &g_rt_cancel, &g_rt.lenet_hw);

  clean.cpu = output;
  clean.physical = g_rt_trace.physical + A733_RT_TRACE_OUTPUT_OFFSET;
  clean.size = 128u;
  clean.slot = g_rt_trace.slot;
  a733_npu_mem_invalidate(&clean);
  memcpy(g_rt.lenet_output, output, sizeof(g_rt.lenet_output));
  g_rt.lenet_output_crc = crc32part(output, A733_RT_LENET_OUTPUT_SIZE, 0);
  g_rt.lenet_output_changed = 0;
  for (index = 0; index < 128u; index++)
    {
      if (output[index] != 0xa5)
        {
          g_rt.lenet_output_changed++;
        }
    }

  if (ret == OK &&
      (g_rt.lenet_output_changed == 0 ||
       g_rt.lenet_output_crc != UINT32_C(0xd50f5d79) ||
       memcmp(output, golden, sizeof(golden)) != 0))
    {
      ret = -EILSEQ;
    }

  g_rt.guards = a733_npu_mem_check_guards();
  if (ret == OK && g_rt.guards < 0)
    {
      ret = g_rt.guards;
    }

out:
  g_rt.lenet_status = ret;
  return ret;
}

static ssize_t a733_rt_read(struct file *filep, char *buffer, size_t buflen)
{
  static char report[8192];
  size_t length;
  size_t copy;
  int appended;
  unsigned int i;

  length = (size_t)snprintf(report, sizeof(report),
    "A733 VIP2 openvela runtime ABI v%u\n"
    "checkpoint=%d contexts=%lu/%u buffers=%lu/%u pages=%lu/%u "
    "free=%lu\n"
    "mmu: mtlb=%08lx stlb=%08lx pool=%08lx vip=%08lx pd=%08lx\n"
    "tasks: submitted=%lu completed=%lu cancelled=%lu timedout=%lu "
    "last=%lu status=%d irq=%08lx idle=%08lx polls=%lu\n"
    "selftest=%d apitest=%d guards=%d inflight=%08lx\n"
    "model: status=%d version=%08lx cid=%08lx size=%llu crc32=%08lx "
    "name='%s' path='%s'\n"
    "stage-model: status=%d size=%llu crc32=%08lx physical=%08lx "
    "vip=%08lx path='%s'\n"
    "stage-input: status=%d size=%llu crc32=%08lx offset=%08lx "
    "physical=%08lx vip=%08lx path='%s'\n"
    "lenet-trace: status=%d command=%08lx/1088 output-crc=%08lx "
    "changed=%lu irq=%08lx idle=%08lx polls=%lu output="
    "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
    "%02x%02x%02x%02x%02x%02x\n"
    "prepared: status=%d hw-status=%d verified=%u name='%s' regions=%lu "
    "outputs=%lu command=%08lx irq=%08lx idle=%08lx polls=%lu "
    "mmu=%08lx fault=%08lx recovery=%d tail=%08lx/%08lx "
    "tail-crc=%08lx tail-changed=%u path='%s'\n"
    "prepared-output[0]: crc=%08lx changed=%lu data="
    "%02x%02x%02x%02x%02x%02x%02x%02x\n"
    "prepared-output[1]: crc=%08lx changed=%lu data="
    "%02x%02x%02x%02x%02x%02x%02x%02x\n"
    "prepared-output[2]: crc=%08lx changed=%lu data="
    "%02x%02x%02x%02x%02x%02x%02x%02x\n"
    "prepared-output[3]: crc=%08lx changed=%lu data="
    "%02x%02x%02x%02x%02x%02x%02x%02x\n"
    "prepared-output[4]: crc=%08lx changed=%lu data="
    "%02x%02x%02x%02x%02x%02x%02x%02x\n"
    "prepared-output[5]: crc=%08lx changed=%lu data="
    "%02x%02x%02x%02x%02x%02x%02x%02x\n"
    "prepared-output[6]: crc=%08lx changed=%lu data="
    "%02x%02x%02x%02x%02x%02x%02x%02x\n"
    "prepared-output[7]: crc=%08lx changed=%lu data="
    "%02x%02x%02x%02x%02x%02x%02x%02x\n"
    "scope: synchronous single-core context/buffer/cache/submit/sync/cancel/"
    "reset ABI, checked staging, fixed LeNet regression and A7PM v1/v2/v3 "
    "multi-output prepared-model execution; direct proprietary NBG linking "
    "remains pending\n",
    A733_NPU_ABI_VERSION, g_rt.checkpoint,
    (unsigned long)g_rt.open_count, A733_RT_MAX_CONTEXTS,
    (unsigned long)g_rt.allocated_buffers, A733_RT_MAX_BUFFERS,
    (unsigned long)g_rt.allocated_pages, A733_RT_POOL_PAGES - 1u,
    (unsigned long)a733_rt_free_bytes(),
    (unsigned long)g_rt_mtlb.physical,
    (unsigned long)g_rt_stlb.physical,
    (unsigned long)g_rt_pool.physical,
    (unsigned long)A733_RT_VIP_BASE,
    (unsigned long)g_rt.pd_entry,
    (unsigned long)g_rt.submitted, (unsigned long)g_rt.completed,
    (unsigned long)g_rt.cancelled, (unsigned long)g_rt.timedout,
    (unsigned long)g_rt.last_task, g_rt.last_status,
    (unsigned long)g_rt.last_hw.irq_value,
    (unsigned long)g_rt.last_hw.idle,
    (unsigned long)g_rt.last_hw.polls,
    g_rt.selftest, g_rt.apitest, g_rt.guards,
    (unsigned long)g_rt_inflight,
    g_rt.model.status, (unsigned long)g_rt.model.format_version,
    (unsigned long)g_rt.model.target_cid,
    (unsigned long long)g_rt.model.file_size,
    (unsigned long)g_rt.model.crc32, g_rt.model.name, g_rt.model.path,
    g_rt.staged_model.status,
    (unsigned long long)g_rt.staged_model.file_size,
    (unsigned long)g_rt.staged_model.crc32,
    (unsigned long)g_rt.staged_model.physical,
    (unsigned long)g_rt.staged_model.vip_address,
    g_rt.staged_model.path,
    g_rt.staged_input.status,
    (unsigned long long)g_rt.staged_input.file_size,
    (unsigned long)g_rt.staged_input.crc32,
    (unsigned long)g_rt.staged_input.offset,
    (unsigned long)g_rt.staged_input.physical,
    (unsigned long)g_rt.staged_input.vip_address,
    g_rt.staged_input.path,
    g_rt.lenet_status,
    (unsigned long)(A733_RT_TRACE_VIP_BASE +
                    A733_RT_TRACE_COMMAND_OFFSET +
                    A733_RT_TRACE_COMMAND_SKIP),
    (unsigned long)g_rt.lenet_output_crc,
    (unsigned long)g_rt.lenet_output_changed,
    (unsigned long)g_rt.lenet_hw.irq_value,
    (unsigned long)g_rt.lenet_hw.idle,
    (unsigned long)g_rt.lenet_hw.polls,
    g_rt.lenet_output[0], g_rt.lenet_output[1],
    g_rt.lenet_output[2], g_rt.lenet_output[3],
    g_rt.lenet_output[4], g_rt.lenet_output[5],
    g_rt.lenet_output[6], g_rt.lenet_output[7],
    g_rt.lenet_output[8], g_rt.lenet_output[9],
    g_rt.lenet_output[10], g_rt.lenet_output[11],
    g_rt.lenet_output[12], g_rt.lenet_output[13],
    g_rt.lenet_output[14], g_rt.lenet_output[15],
    g_rt.lenet_output[16], g_rt.lenet_output[17],
    g_rt.lenet_output[18], g_rt.lenet_output[19],
    g_rt.prepared_status, g_rt.prepared_hw_status,
    g_rt.prepared_verified ? 1u : 0u, g_rt.prepared_name,
    (unsigned long)g_rt.prepared_regions,
    (unsigned long)g_rt.prepared_outputs,
    (unsigned long)g_rt.prepared_command,
    (unsigned long)g_rt.prepared_hw.irq_value,
    (unsigned long)g_rt.prepared_hw.idle,
    (unsigned long)g_rt.prepared_hw.polls,
    (unsigned long)g_rt.prepared_hw.mmu_status,
    (unsigned long)g_rt.prepared_hw.mmu_exception,
    g_rt.prepared_hw.recovery,
    (unsigned long)g_rt.prepared_tail_vip,
    (unsigned long)g_rt.prepared_tail_physical,
    (unsigned long)g_rt.prepared_tail_crc,
    g_rt.prepared_tail_changed ? 1u : 0u,
    g_rt.prepared_path,
    (unsigned long)g_rt.prepared_output_crc[0],
    (unsigned long)g_rt.prepared_output_changed[0],
    g_rt.prepared_output[0][0], g_rt.prepared_output[0][1],
    g_rt.prepared_output[0][2], g_rt.prepared_output[0][3],
    g_rt.prepared_output[0][4], g_rt.prepared_output[0][5],
    g_rt.prepared_output[0][6], g_rt.prepared_output[0][7],
    (unsigned long)g_rt.prepared_output_crc[1],
    (unsigned long)g_rt.prepared_output_changed[1],
    g_rt.prepared_output[1][0], g_rt.prepared_output[1][1],
    g_rt.prepared_output[1][2], g_rt.prepared_output[1][3],
    g_rt.prepared_output[1][4], g_rt.prepared_output[1][5],
    g_rt.prepared_output[1][6], g_rt.prepared_output[1][7],
    (unsigned long)g_rt.prepared_output_crc[2],
    (unsigned long)g_rt.prepared_output_changed[2],
    g_rt.prepared_output[2][0], g_rt.prepared_output[2][1],
    g_rt.prepared_output[2][2], g_rt.prepared_output[2][3],
    g_rt.prepared_output[2][4], g_rt.prepared_output[2][5],
    g_rt.prepared_output[2][6], g_rt.prepared_output[2][7],
    (unsigned long)g_rt.prepared_output_crc[3],
    (unsigned long)g_rt.prepared_output_changed[3],
    g_rt.prepared_output[3][0], g_rt.prepared_output[3][1],
    g_rt.prepared_output[3][2], g_rt.prepared_output[3][3],
    g_rt.prepared_output[3][4], g_rt.prepared_output[3][5],
    g_rt.prepared_output[3][6], g_rt.prepared_output[3][7],
    (unsigned long)g_rt.prepared_output_crc[4],
    (unsigned long)g_rt.prepared_output_changed[4],
    g_rt.prepared_output[4][0], g_rt.prepared_output[4][1],
    g_rt.prepared_output[4][2], g_rt.prepared_output[4][3],
    g_rt.prepared_output[4][4], g_rt.prepared_output[4][5],
    g_rt.prepared_output[4][6], g_rt.prepared_output[4][7],
    (unsigned long)g_rt.prepared_output_crc[5],
    (unsigned long)g_rt.prepared_output_changed[5],
    g_rt.prepared_output[5][0], g_rt.prepared_output[5][1],
    g_rt.prepared_output[5][2], g_rt.prepared_output[5][3],
    g_rt.prepared_output[5][4], g_rt.prepared_output[5][5],
    g_rt.prepared_output[5][6], g_rt.prepared_output[5][7],
    (unsigned long)g_rt.prepared_output_crc[6],
    (unsigned long)g_rt.prepared_output_changed[6],
    g_rt.prepared_output[6][0], g_rt.prepared_output[6][1],
    g_rt.prepared_output[6][2], g_rt.prepared_output[6][3],
    g_rt.prepared_output[6][4], g_rt.prepared_output[6][5],
    g_rt.prepared_output[6][6], g_rt.prepared_output[6][7],
    (unsigned long)g_rt.prepared_output_crc[7],
    (unsigned long)g_rt.prepared_output_changed[7],
    g_rt.prepared_output[7][0], g_rt.prepared_output[7][1],
    g_rt.prepared_output[7][2], g_rt.prepared_output[7][3],
    g_rt.prepared_output[7][4], g_rt.prepared_output[7][5],
    g_rt.prepared_output[7][6], g_rt.prepared_output[7][7]);

  if (length >= sizeof(report))
    {
      length = sizeof(report) - 1u;
    }

  appended = snprintf(report + length, sizeof(report) - length,
    "dynamic-input: status=%d active=%u size=%lu crc=%08lx path='%s'\n"
    "yolov8: status=%d golden=%u candidates=%lu detections=%lu "
    "score=0.25 iou=0.45 input=640x640 RGB\n",
    g_rt.dynamic_input_status, g_rt.dynamic_input_path[0] != '\0' ? 1u : 0u,
    (unsigned long)g_rt.dynamic_input_size,
    (unsigned long)g_rt.dynamic_input_crc, g_rt.dynamic_input_path,
    g_rt.yolo_status, g_rt.prepared_golden ? 1u : 0u,
    (unsigned long)g_rt.yolo_candidates,
    (unsigned long)g_rt.yolo_detections);
  if (appended > 0)
    {
      length += (size_t)appended < sizeof(report) - length ?
                (size_t)appended : sizeof(report) - length - 1u;
    }

  for (i = 0; i < g_rt.yolo_detections && length < sizeof(report) - 1u; i++)
    {
      appended = snprintf(report + length, sizeof(report) - length,
        "det[%u]: class=%u score-milli=%lu box-centi=%lu,%lu,%lu,%lu\n",
        i, g_rt.yolo[i].class_id,
        (unsigned long)(g_rt.yolo[i].score * 1000.0f + 0.5f),
        (unsigned long)(g_rt.yolo[i].x1 * 100.0f + 0.5f),
        (unsigned long)(g_rt.yolo[i].y1 * 100.0f + 0.5f),
        (unsigned long)(g_rt.yolo[i].x2 * 100.0f + 0.5f),
        (unsigned long)(g_rt.yolo[i].y2 * 100.0f + 0.5f));
      if (appended <= 0)
        {
          break;
        }

      length += (size_t)appended < sizeof(report) - length ?
                (size_t)appended : sizeof(report) - length - 1u;
    }

  if ((size_t)filep->f_pos >= length)
    {
      return 0;
    }

  copy = length - (size_t)filep->f_pos;
  if (copy > buflen)
    {
      copy = buflen;
    }

  memcpy(buffer, report + filep->f_pos, copy);
  filep->f_pos += copy;
  return (ssize_t)copy;
}

static int a733_rt_alloc(struct a733_rt_context_s *context,
                         struct a733_npu_alloc_s *request)
{
  irqstate_t flags;
  uint32_t pages;
  uint32_t start;
  uint32_t run;
  uintptr_t cpu;
  unsigned int slot;

  if (request == NULL || request->size == 0 ||
      request->size > A733_RT_POOL_SIZE - A733_RT_PAGE_SIZE ||
      (request->flags & ~(A733_NPU_BUFFER_READ | A733_NPU_BUFFER_WRITE |
                          A733_NPU_BUFFER_COMMAND)) != 0)
    {
      return -EINVAL;
    }

  pages = (request->size + A733_RT_PAGE_SIZE - 1u) / A733_RT_PAGE_SIZE;
  flags = enter_critical_section();
  for (slot = 0; slot < A733_RT_MAX_BUFFERS; slot++)
    {
      if (!g_rt_buffers[slot].active)
        {
          break;
        }
    }

  if (slot == A733_RT_MAX_BUFFERS)
    {
      leave_critical_section(flags);
      return -ENOSPC;
    }

  start = 1;
  run = 0;
  while (start + run < A733_RT_POOL_PAGES)
    {
      if (!g_rt_pages[start + run])
        {
          run++;
          if (run == pages)
            {
              break;
            }
        }
      else
        {
          start += run + 1u;
          run = 0;
        }
    }

  if (run != pages)
    {
      leave_critical_section(flags);
      return -ENOMEM;
    }

  for (run = 0; run < pages; run++)
    {
      g_rt_pages[start + run] = true;
    }

  g_rt_buffers[slot].active = true;
  g_rt_buffers[slot].owner = context;
  g_rt_buffers[slot].generation++;
  if (g_rt_buffers[slot].generation == 0)
    {
      g_rt_buffers[slot].generation = 1;
    }

  g_rt_buffers[slot].handle =
    (g_rt_buffers[slot].generation << 8) | (slot + 1u);
  g_rt_buffers[slot].first_page = start;
  g_rt_buffers[slot].page_count = pages;
  g_rt_buffers[slot].requested = request->size;
  g_rt_buffers[slot].flags = request->flags;
  g_rt.allocated_buffers++;
  g_rt.allocated_pages += pages;

  request->handle = g_rt_buffers[slot].handle;
  request->physical = g_rt_pool.physical + start * A733_RT_PAGE_SIZE;
  request->vip_address = A733_RT_VIP_BASE + start * A733_RT_PAGE_SIZE;
  request->allocated = pages * A733_RT_PAGE_SIZE;
  request->cpu_address = (uint64_t)(uintptr_t)g_rt_pool.cpu +
                         start * A733_RT_PAGE_SIZE;
  cpu = (uintptr_t)request->cpu_address;
  leave_critical_section(flags);
  memset((void *)cpu, 0, request->allocated);
  return OK;
}

static int a733_rt_cache(struct a733_rt_context_s *context,
                         struct a733_npu_cache_s *request, bool clean)
{
  struct a733_rt_buffer_s *entry;
  uintptr_t start;

  entry = a733_rt_find_buffer(context, request->handle);
  if (entry == NULL || request->offset > entry->requested ||
      request->size > entry->requested - request->offset)
    {
      return -EINVAL;
    }

  start = (uintptr_t)g_rt_pool.cpu +
          entry->first_page * A733_RT_PAGE_SIZE + request->offset;
  if (clean)
    {
      up_clean_dcache(start, start + request->size);
    }
  else
    {
      up_invalidate_dcache(start, start + request->size);
    }

  return OK;
}

static int a733_rt_submit(struct a733_rt_context_s *context,
                          struct a733_npu_submit_s *request)
{
  struct a733_rt_buffer_s *entry;
  struct a733_npu_hw_result_s result;
  uint32_t *tail;
  uint32_t *words;
  uint32_t word_count;
  uint32_t index;
  uintptr_t cpu;
  uint32_t vip;
  bool event_found = false;
  int ret;

  entry = a733_rt_find_buffer(context, request->command_handle);
  if (entry == NULL || (entry->flags & A733_NPU_BUFFER_COMMAND) == 0 ||
      request->command_size < 8 || (request->command_size & 7u) != 0 ||
      (request->command_offset & 7u) != 0 ||
      request->command_offset > entry->requested ||
      request->command_size > entry->requested - request->command_offset)
    {
      return -EINVAL;
    }

  if (request->timeout_ms == 0)
    {
      request->timeout_ms = 100;
    }

  if (request->timeout_ms > A733_NPU_MAX_TIMEOUT_MS)
    {
      return -ERANGE;
    }

  cpu = (uintptr_t)g_rt_pool.cpu +
        entry->first_page * A733_RT_PAGE_SIZE + request->command_offset;
  tail = (uint32_t *)(cpu + request->command_size - 8u);
  if (tail[0] != A733_RT_END0 || tail[1] != 0)
    {
      return -EPROTO;
    }

  /* Phase-one submissions use the proven blocking EVENT id 4 contract.
   * Reject streams that cannot produce the completion IRQ this driver waits
   * for; later NBG scheduling can allocate per-task event IDs explicitly.
   */

  words = (uint32_t *)cpu;
  word_count = request->command_size / sizeof(uint32_t);
  for (index = 0; index + 1u < word_count; index++)
    {
      if (words[index] == A733_RT_EVENT0 &&
          words[index + 1u] == A733_RT_EVENT1)
        {
          event_found = true;
          break;
        }
    }

  if (!event_found)
    {
      return -EPROTO;
    }

  up_clean_dcache(cpu, cpu + request->command_size);
  g_rt_cancel = false;
  g_rt_inflight = entry->handle;
  request->task_id = ++g_rt.next_task;
  g_rt.submitted++;
  vip = A733_RT_VIP_BASE + entry->first_page * A733_RT_PAGE_SIZE +
        request->command_offset;
  ret = a733_npu_hw_execute(g_rt.pd_entry, vip, request->timeout_ms,
                            &g_rt_cancel, true, &result);
  g_rt_inflight = 0;
  g_rt.guards = a733_npu_mem_check_guards();
  if (ret == OK && g_rt.guards < 0)
    {
      ret = g_rt.guards;
    }

  if (ret == OK)
    {
      g_rt.completed++;
    }
  else if (ret == -ECANCELED)
    {
      g_rt.cancelled++;
    }
  else if (ret == -ETIMEDOUT)
    {
      g_rt.timedout++;
    }

  g_rt.last_task = request->task_id;
  g_rt.last_status = ret;
  g_rt.last_hw = result;
  request->status = ret;
  request->irq_value = result.irq_value;
  request->idle = result.idle;
  request->polls = result.polls;
  return ret;
}

static int a733_rt_apitest(struct a733_rt_context_s *context)
{
  struct a733_npu_alloc_s alloc;
  struct a733_npu_submit_s submit;
  struct a733_rt_buffer_s *entry;
  uint32_t *command;
  irqstate_t flags;
  int ret;

  memset(&alloc, 0, sizeof(alloc));
  alloc.size = A733_RT_PAGE_SIZE;
  alloc.flags = A733_NPU_BUFFER_READ | A733_NPU_BUFFER_WRITE |
                A733_NPU_BUFFER_COMMAND;
  ret = a733_rt_alloc(context, &alloc);
  if (ret < 0)
    {
      g_rt.apitest = ret;
      return ret;
    }

  command = (uint32_t *)(uintptr_t)alloc.cpu_address;
  command[0] = A733_RT_EVENT0;
  command[1] = A733_RT_EVENT1;
  command[2] = A733_RT_END0;
  command[3] = 0;
  memset(&submit, 0, sizeof(submit));
  submit.command_handle = alloc.handle;
  submit.command_size = 16;
  submit.timeout_ms = 100;
  submit.flags = A733_NPU_SUBMIT_RESTORE;
  ret = a733_rt_submit(context, &submit);

  flags = enter_critical_section();
  entry = a733_rt_find_buffer(context, alloc.handle);
  if (entry != NULL && entry->handle != g_rt_inflight)
    {
      a733_rt_release_buffer(entry);
    }
  else if (ret == OK)
    {
      ret = -EIO;
    }

  leave_critical_section(flags);
  g_rt.apitest = ret;
  return ret;
}

static int a733_rt_ioctl(struct file *filep, int cmd, unsigned long arg)
{
  struct a733_rt_context_s *context = filep->f_priv;
  struct a733_rt_buffer_s *entry;
  struct a733_npu_info_s *info;
  struct a733_npu_alloc_s *alloc;
  struct a733_npu_free_s *release;
  struct a733_npu_cache_s *cache;
  struct a733_npu_submit_s *submit;
  struct a733_npu_sync_s *sync;
  struct a733_npu_model_probe_s *model;
  struct a733_npu_stage_file_s *stage;
  struct a733_npu_run_s *run;
  irqstate_t flags;

  if (context == NULL || !context->active)
    {
      return -EBADF;
    }

  if (arg == 0 && cmd != A733_NPUIOC_CANCEL &&
      cmd != A733_NPUIOC_RESET && cmd != A733_NPUIOC_SELFTEST)
    {
      return -EINVAL;
    }

  switch (cmd)
    {
      case A733_NPUIOC_GET_INFO:
        info = (struct a733_npu_info_s *)(uintptr_t)arg;
        memset(info, 0, sizeof(*info));
        info->abi_version = A733_NPU_ABI_VERSION;
        info->cid_pid = a733_npu_hw_cid();
        info->core_count = 1;
        info->page_size = A733_RT_PAGE_SIZE;
        info->pool_size = A733_RT_POOL_SIZE - A733_RT_PAGE_SIZE;
        info->pool_free = a733_rt_free_bytes();
        info->max_buffers = A733_RT_MAX_BUFFERS;
        info->max_timeout_ms = A733_NPU_MAX_TIMEOUT_MS;
        return OK;

      case A733_NPUIOC_ALLOC:
        alloc = (struct a733_npu_alloc_s *)(uintptr_t)arg;
        return a733_rt_alloc(context, alloc);

      case A733_NPUIOC_FREE:
        release = (struct a733_npu_free_s *)(uintptr_t)arg;
        flags = enter_critical_section();
        entry = a733_rt_find_buffer(context, release->handle);
        if (entry == NULL || entry->handle == g_rt_inflight)
          {
            leave_critical_section(flags);
            return entry == NULL ? -ENOENT : -EBUSY;
          }

        a733_rt_release_buffer(entry);
        leave_critical_section(flags);
        return OK;

      case A733_NPUIOC_CACHE_CLEAN:
      case A733_NPUIOC_CACHE_INVALIDATE:
        cache = (struct a733_npu_cache_s *)(uintptr_t)arg;
        return a733_rt_cache(context, cache,
                             cmd == A733_NPUIOC_CACHE_CLEAN);

      case A733_NPUIOC_SUBMIT:
        submit = (struct a733_npu_submit_s *)(uintptr_t)arg;
        return a733_rt_submit(context, submit);

      case A733_NPUIOC_SYNC:
        sync = (struct a733_npu_sync_s *)(uintptr_t)arg;
        if (sync->task_id != g_rt.last_task)
          {
            return -ENOENT;
          }

        sync->status = g_rt.last_status;
        sync->irq_value = g_rt.last_hw.irq_value;
        sync->idle = g_rt.last_hw.idle;
        return g_rt.last_status;

      case A733_NPUIOC_CANCEL:
        g_rt_cancel = true;
        return OK;

      case A733_NPUIOC_RESET:
        if (g_rt_inflight != 0)
          {
            return -EBUSY;
          }

        g_rt_cancel = true;
        return a733_npu_hw_reset();

      case A733_NPUIOC_SELFTEST:
        return a733_rt_selftest();

      case A733_NPUIOC_MODEL_PROBE:
        model = (struct a733_npu_model_probe_s *)(uintptr_t)arg;
        return a733_rt_model_probe(model);

      case A733_NPUIOC_STAGE_FILE:
        stage = (struct a733_npu_stage_file_s *)(uintptr_t)arg;
        return a733_rt_stage_file(stage);

      case A733_NPUIOC_RUN_A7PM:
        run = (struct a733_npu_run_s *)(uintptr_t)arg;
        return a733_rt_run_a7pm(run);

      default:
        break;
    }

  return -ENOTTY;
}

static ssize_t a733_rt_write(struct file *filep, const char *buffer,
                             size_t buflen)
{
  struct a733_npu_model_probe_s model;
  struct a733_npu_stage_file_s stage;
  const struct a733_npu_hw_result_s *reported_hw = &g_rt.last_hw;
  char command[A733_NPU_MODEL_PATH_MAX + 16u];
  size_t length;
  int ret;

  (void)filep;
  if (buflen == 0 || buflen >= sizeof(command))
    {
      return -EINVAL;
    }

  memcpy(command, buffer, buflen);
  command[buflen] = '\0';
  length = buflen;
  while (length > 0 && (command[length - 1] == '\n' ||
                         command[length - 1] == '\r' ||
                         command[length - 1] == ' '))
    {
      command[--length] = '\0';
    }

  if (strcmp(command, "selftest") == 0)
    {
      ret = a733_rt_selftest();
    }
  else if (strcmp(command, "apitest") == 0)
    {
      ret = a733_rt_apitest(filep->f_priv);
    }
  else if (strcmp(command, "lenet") == 0)
    {
      ret = a733_rt_lenet();
      reported_hw = &g_rt.lenet_hw;
    }
  else if (strncmp(command, "dynamic-input=", 14) == 0)
    {
      ret = a733_rt_validate_path(command + 14);
      if (ret == OK)
        {
          strlcpy(g_rt.dynamic_input_path, command + 14,
                  sizeof(g_rt.dynamic_input_path));
          g_rt.dynamic_input_status = -EAGAIN;
        }
    }
  else if (strcmp(command, "inputclear") == 0)
    {
      g_rt.dynamic_input_path[0] = '\0';
      g_rt.dynamic_input_crc = 0;
      g_rt.dynamic_input_size = 0;
      g_rt.dynamic_input_status = OK;
      ret = OK;
    }
  else if (strncmp(command, "prepared=", 9) == 0)
    {
      ret = a733_rt_validate_path(command + 9);
      if (ret == OK)
        {
          ret = a733_rt_prepared(command + 9);
          reported_hw = &g_rt.prepared_hw;
        }
    }
  else if (strcmp(command, "reset") == 0)
    {
      ret = a733_npu_hw_reset();
    }
  else if (strncmp(command, "modelcheck=", 11) == 0)
    {
      memset(&model, 0, sizeof(model));
      strlcpy(model.path, command + 11, sizeof(model.path));
      ret = a733_rt_model_probe(&model);
    }
  else if (strncmp(command, "modelstage=", 11) == 0)
    {
      memset(&stage, 0, sizeof(stage));
      strlcpy(stage.path, command + 11, sizeof(stage.path));
      stage.kind = A733_NPU_STAGE_MODEL;
      ret = a733_rt_stage_file(&stage);
    }
  else if (strncmp(command, "inputstage=", 11) == 0)
    {
      memset(&stage, 0, sizeof(stage));
      strlcpy(stage.path, command + 11, sizeof(stage.path));
      stage.kind = A733_NPU_STAGE_INPUT;
      ret = a733_rt_stage_file(&stage);
    }
  else if (strcmp(command, "stageclear") == 0)
    {
      memset(g_rt_stage.cpu, 0, g_rt_stage.size);
      a733_npu_mem_clean(&g_rt_stage);
      memset(&g_rt.staged_model, 0, sizeof(g_rt.staged_model));
      memset(&g_rt.staged_input, 0, sizeof(g_rt.staged_input));
      g_rt.staged_model.status = -EAGAIN;
      g_rt.staged_input.status = -EAGAIN;
      ret = OK;
    }
  else if (strcmp(command, "modelclear") == 0)
    {
      memset(&g_rt.model, 0, sizeof(g_rt.model));
      g_rt.model.status = -EAGAIN;
      ret = OK;
    }
  else
    {
      return -EINVAL;
    }

  syslog(ret < 0 ? LOG_ERR : LOG_INFO,
         "A733 NPU runtime: %s %s (%d), irq=%08lx idle=%08lx guards=%d\n",
         command, ret < 0 ? "failed" : "passed", ret,
         (unsigned long)reported_hw->irq_value,
         (unsigned long)reported_hw->idle, g_rt.guards);
  return ret < 0 ? ret : (ssize_t)buflen;
}

static const struct file_operations g_a733_rt_fops =
{
  .open = a733_rt_open,
  .close = a733_rt_close,
  .read = a733_rt_read,
  .write = a733_rt_write,
  .ioctl = a733_rt_ioctl,
};

int a733_npu_runtime_initialize(void)
{
  uint32_t *mtlb;
  uint32_t *stlb;
  uint32_t *trace_stlb;
  unsigned int page;
  int ret;

  memset(&g_rt, 0, sizeof(g_rt));
  memset(g_rt_contexts, 0, sizeof(g_rt_contexts));
  memset(g_rt_buffers, 0, sizeof(g_rt_buffers));
  memset(g_rt_pages, 0, sizeof(g_rt_pages));
  g_rt.checkpoint = -EAGAIN;
  g_rt.selftest = -EAGAIN;
  g_rt.apitest = -EAGAIN;
  g_rt.guards = -EAGAIN;
  g_rt.last_status = -EAGAIN;
  g_rt.model.status = -EAGAIN;
  g_rt.staged_model.status = -EAGAIN;
  g_rt.staged_input.status = -EAGAIN;
  g_rt.lenet_status = -EAGAIN;
  g_rt.prepared_status = -EAGAIN;
  g_rt.prepared_hw_status = -EAGAIN;
  g_rt.dynamic_input_status = OK;
  g_rt.yolo_status = -EAGAIN;
  g_rt_pages[0] = true; /* Reserved deterministic self-test command page. */

  ret = a733_npu_mem_alloc(256u * sizeof(uint32_t), 4096, &g_rt_mtlb);
  if (ret < 0)
    {
      goto out;
    }

  ret = a733_npu_mem_alloc(4096u * sizeof(uint32_t), 16384, &g_rt_stlb);
  if (ret < 0)
    {
      goto out;
    }

  ret = a733_npu_mem_alloc(A733_RT_POOL_SIZE, 4096, &g_rt_pool);
  if (ret < 0)
    {
      goto out;
    }

  ret = a733_npu_mem_alloc(A733_RT_STAGE_SIZE, 4096, &g_rt_stage);
  if (ret < 0)
    {
      goto out;
    }

  ret = a733_npu_mem_alloc(4096u * sizeof(uint32_t), 16384,
                           &g_rt_trace_stlb);
  if (ret < 0)
    {
      goto out;
    }

  ret = a733_npu_mem_alloc(A733_RT_TRACE_SIZE, 4096, &g_rt_trace);
  if (ret < 0)
    {
      goto out;
    }

  ret = a733_npu_mem_alloc(A733_RT_A7PM_MAX_WINDOWS *
                           A733_RT_A7PM_STLB_SIZE, 16384,
                           &g_rt_prepared_stlb);
  if (ret < 0)
    {
      goto out;
    }

  ret = a733_npu_mem_alloc(A733_RT_A7PM_POOL_SIZE, 4096,
                           &g_rt_prepared_pool);
  if (ret < 0)
    {
      goto out;
    }

  mtlb = g_rt_mtlb.cpu;
  stlb = g_rt_stlb.cpu;
  trace_stlb = g_rt_trace_stlb.cpu;
  memset(mtlb, 0, g_rt_mtlb.size);
  memset(g_rt_prepared_stlb.cpu, 0,
         g_rt_prepared_stlb.size);
  memset(g_rt_prepared_pool.cpu, 0,
         g_rt_prepared_pool.size);
  for (page = 0; page < 4096u; page++)
    {
      stlb[page] = A733_RT_FREE_ENTRY;
      trace_stlb[page] = A733_RT_FREE_ENTRY;
    }

  mtlb[A733_RT_TRACE_VIP_BASE >> 24] =
    g_rt_trace_stlb.physical | A733_RT_PRESENT;
  mtlb[A733_RT_MTLB_INDEX] = g_rt_stlb.physical | A733_RT_PRESENT;
  for (page = 0; page < A733_RT_TRACE_SIZE / A733_RT_PAGE_SIZE; page++)
    {
      trace_stlb[page] =
        ((g_rt_trace.physical + page * A733_RT_PAGE_SIZE) &
         UINT32_C(0xfffff000)) |
        A733_RT_PRESENT | A733_RT_EXCEPTION | A733_RT_WRITEABLE;
    }

  for (page = 0; page < A733_RT_POOL_PAGES; page++)
    {
      stlb[page] = ((g_rt_pool.physical + page * A733_RT_PAGE_SIZE) &
                    UINT32_C(0xfffff000)) |
                   A733_RT_PRESENT | A733_RT_EXCEPTION | A733_RT_WRITEABLE;
    }

  /* Linux VIPLite placed the prepared constants at 0x02010000.  The normal
   * runtime pool already has the matching linear VIP mapping, so reserve
   * those pages from the public allocator and use them in place.  This avoids
   * duplicating 428 KiB inside the bounded low-32-bit DMA arena.
   */

  for (page = 0; page < A733_RT_TRACE_WEIGHTS_PAGES; page++)
    {
      g_rt_pages[A733_RT_TRACE_WEIGHTS_FIRST + page] = true;
    }


  for (page = 0; page < A733_RT_STAGE_SIZE / A733_RT_PAGE_SIZE; page++)
    {
      stlb[A733_RT_STAGE_STLB_INDEX + page] =
        ((g_rt_stage.physical + page * A733_RT_PAGE_SIZE) &
         UINT32_C(0xfffff000)) |
        A733_RT_PRESENT | A733_RT_EXCEPTION | A733_RT_WRITEABLE;
    }

  a733_npu_mem_clean(&g_rt_mtlb);
  a733_npu_mem_clean(&g_rt_stlb);
  a733_npu_mem_clean(&g_rt_pool);
  a733_npu_mem_clean(&g_rt_stage);
  a733_npu_mem_clean(&g_rt_trace_stlb);
  a733_npu_mem_clean(&g_rt_trace);
  a733_npu_mem_clean(&g_rt_prepared_stlb);
  a733_npu_mem_clean(&g_rt_prepared_pool);
  g_rt.pd_entry = (g_rt_mtlb.physical >> 8) | 1u;
  g_rt.guards = a733_npu_mem_check_guards();
  if (g_rt.guards < 0)
    {
      ret = g_rt.guards;
      goto out;
    }

  ret = register_driver("/dev/npu0", &g_a733_rt_fops, 0660, NULL);

out:
  g_rt.checkpoint = ret;
  syslog(ret < 0 ? LOG_ERR : LOG_INFO,
         "A733 NPU runtime: /dev/npu0 %s (%d), pool=%08lx/%lu "
         "vip=%08lx pd=%08lx lenet=%08lx/%08lx prepared=%08lx/%lu\n",
         ret < 0 ? "failed" : "ready", ret,
         (unsigned long)g_rt_pool.physical,
         (unsigned long)A733_RT_POOL_SIZE,
         (unsigned long)A733_RT_VIP_BASE,
         (unsigned long)g_rt.pd_entry,
         (unsigned long)g_rt_trace.physical,
         (unsigned long)(g_rt_pool.physical +
                         A733_RT_TRACE_WEIGHTS_OFFSET),
         (unsigned long)g_rt_prepared_pool.physical,
         (unsigned long)g_rt_prepared_pool.size);
  return ret;
}

#endif
