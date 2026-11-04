/****************************************************************************
 * apps/system/a733npu/a733npu_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <nuttx/npu/a733_vip2.h>

#include <system/a733_vision.h>

#define NPU_DEVICE "/dev/npu0"
#define NPU_YOLOV8_PACKAGE "/data/npu/yolov8n-pcq-v3.a7pm"

static uint32_t g_vision_sequence;

_Static_assert(sizeof(struct a733_vision_message_s) == 32,
               "vision IPC must match CONFIG_MQ_MAXMSGSIZE");

static void npu_usage(void)
{
  puts("Usage:");
  puts("  npu info");
  puts("  npu selftest");
  puts("  npu report");
  puts("  npu run <model.a7pm> [input.bin]");
  puts("  npu yolo <640x640-rgb.bin>");
  puts("  npu yolo-ui <640x640-rgb.bin>");
}

static uint64_t npu_micros(const struct timespec *value)
{
  return (uint64_t)value->tv_sec * UINT64_C(1000000) +
         (uint64_t)value->tv_nsec / UINT64_C(1000);
}

static int npu_info(int fd)
{
  struct a733_npu_info_s info;

  memset(&info, 0, sizeof(info));
  if (ioctl(fd, A733_NPUIOC_GET_INFO,
            (unsigned long)(uintptr_t)&info) < 0)
    {
      fprintf(stderr, "npu: GET_INFO failed: %d\n", errno);
      return 1;
    }

  printf("A733 VIP2 ABI v%lu CID=%08lx cores=%lu page=%lu "
         "pool=%lu free=%lu buffers=%lu timeout=%lu ms\n",
         (unsigned long)info.abi_version,
         (unsigned long)info.cid_pid,
         (unsigned long)info.core_count,
         (unsigned long)info.page_size,
         (unsigned long)info.pool_size,
         (unsigned long)info.pool_free,
         (unsigned long)info.max_buffers,
         (unsigned long)info.max_timeout_ms);
  return 0;
}

static int npu_report(int fd)
{
  char buffer[512];
  ssize_t length;

  while ((length = read(fd, buffer, sizeof(buffer))) > 0)
    {
      if (fwrite(buffer, 1, (size_t)length, stdout) != (size_t)length)
        {
          return 1;
        }
    }

  return length < 0 ? 1 : 0;
}

static uint16_t npu_vision_coord(int32_t centi)
{
  int32_t pixel;

  if (centi <= 0)
    {
      return 0;
    }

  pixel = (centi + 50) / 100;
  return pixel > A733_VISION_MODEL_SIZE ? A733_VISION_MODEL_SIZE :
         (uint16_t)pixel;
}

static void npu_publish_vision(const struct a733_npu_run_s *run,
                               uint64_t elapsed)
{
  struct a733_vision_message_s message;
  unsigned int count;
  unsigned int i;
  mqd_t queue;

  queue = mq_open(A733_VISION_QUEUE_NAME, O_WRONLY | O_NONBLOCK);
  if (queue == (mqd_t)-1)
    {
      printf("npu: vision UI not listening (%d)\n", errno);
      return;
    }

  count = run->detection_count;
  if (count > A733_VISION_MAX_BOXES)
    {
      count = A733_VISION_MAX_BOXES;
    }

  memset(&message, 0, sizeof(message));
  message.magic = A733_VISION_MAGIC;
  uint32_t sequence=__atomic_add_fetch(&g_vision_sequence,1,__ATOMIC_RELAXED);
  if(!sequence)sequence=__atomic_add_fetch(&g_vision_sequence,1,__ATOMIC_RELAXED);
  message.sequence = sequence;
  message.version = A733_VISION_VERSION;
  message.type = A733_VISION_FRAME;
  message.count = count;
  message.x1 = (uint16_t)run->input_crc32;
  message.y1 = (uint16_t)(run->input_crc32 >> 16);
  message.x2 = A733_VISION_MODEL_SIZE;
  message.y2 = A733_VISION_MODEL_SIZE;
  message.value = elapsed > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed;
  if (mq_send(queue, (const char *)&message, sizeof(message), 0) < 0)
    {
      printf("npu: vision frame dropped (%d)\n", errno);
      mq_close(queue);
      return;
    }

  for (i = 0; i < count; i++)
    {
      const struct a733_npu_detection_s *d = &run->detections[i];

      memset(&message, 0, sizeof(message));
      message.magic = A733_VISION_MAGIC;
      message.sequence = sequence;
      message.version = A733_VISION_VERSION;
      message.type = A733_VISION_DETECTION;
      message.index = i;
      message.count = count;
      message.class_id = d->class_id;
      message.score_milli = d->score_milli;
      message.x1 = npu_vision_coord(d->x1_centi);
      message.y1 = npu_vision_coord(d->y1_centi);
      message.x2 = npu_vision_coord(d->x2_centi);
      message.y2 = npu_vision_coord(d->y2_centi);
      message.value = run->input_crc32;
      if (mq_send(queue, (const char *)&message, sizeof(message), 0) < 0)
        {
          printf("npu: vision detection %u dropped (%d)\n", i, errno);
          break;
        }
    }

  printf("npu: vision sequence=%lu boxes=%u queued=%u\n",
         (unsigned long)sequence, count, i);
  mq_close(queue);
}

static int npu_run(int fd, const char *package, const char *input,
                   bool publish_vision)
{
  struct a733_npu_run_s run;
  struct timespec before;
  struct timespec after;
  uint64_t elapsed;
  unsigned int i;
  int call_status;
  int call_errno;

  memset(&run, 0, sizeof(run));
  if (strlcpy(run.package_path, package, sizeof(run.package_path)) >=
      sizeof(run.package_path))
    {
      fputs("npu: package path is too long\n", stderr);
      return 1;
    }

  if (input != NULL)
    {
      if (strlcpy(run.input_path, input, sizeof(run.input_path)) >=
          sizeof(run.input_path))
        {
          fputs("npu: input path is too long\n", stderr);
          return 1;
        }

      run.flags = A733_NPU_RUN_INPUT_FILE;
    }

  clock_gettime(CLOCK_MONOTONIC, &before);
  call_status = ioctl(fd, A733_NPUIOC_RUN_A7PM,
                      (unsigned long)(uintptr_t)&run);
  call_errno = errno;
  clock_gettime(CLOCK_MONOTONIC, &after);
  elapsed = npu_micros(&after) - npu_micros(&before);

  printf("status=%ld hw=%ld elapsed=%llu us irq=%08lx idle=%08lx "
         "polls=%lu input=%lu/%08lx outputs=%lu candidates=%lu "
         "detections=%lu\n",
         (long)run.status, (long)run.hardware_status,
         (unsigned long long)elapsed,
         (unsigned long)run.irq_value, (unsigned long)run.idle,
         (unsigned long)run.polls, (unsigned long)run.input_size,
         (unsigned long)run.input_crc32,
         (unsigned long)run.output_count,
         (unsigned long)run.candidate_count,
         (unsigned long)run.detection_count);

  for (i = 0; i < run.output_count &&
              i < A733_NPU_RUN_MAX_OUTPUTS; i++)
    {
      printf("output[%u]: crc=%08lx changed=%lu\n", i,
             (unsigned long)run.output_crc32[i],
             (unsigned long)run.output_changed[i]);
    }

  for (i = 0; i < run.detection_count &&
              i < A733_NPU_RUN_MAX_DETECTIONS; i++)
    {
      const struct a733_npu_detection_s *d = &run.detections[i];
      printf("det[%u]: class=%u score=%u.%03u "
             "box=%ld.%02ld,%ld.%02ld,%ld.%02ld,%ld.%02ld\n",
             i, (unsigned int)d->class_id,
             (unsigned int)d->score_milli / 1000,
             (unsigned int)d->score_milli % 1000,
             (long)(d->x1_centi / 100), (long)(d->x1_centi % 100),
             (long)(d->y1_centi / 100), (long)(d->y1_centi % 100),
             (long)(d->x2_centi / 100), (long)(d->x2_centi % 100),
             (long)(d->y2_centi / 100), (long)(d->y2_centi % 100));
    }

  if (call_status < 0)
    {
      fprintf(stderr, "npu: inference ioctl failed: errno=%d status=%ld\n",
              call_errno, (long)run.status);
      return 1;
    }

  if (publish_vision)
    {
      npu_publish_vision(&run, elapsed);
    }

  return 0;
}

int main(int argc, char **argv)
{
  int fd;
  int ret;

  if (argc < 2)
    {
      npu_usage();
      return 1;
    }

  fd = open(NPU_DEVICE, O_RDWR | O_CLOEXEC);
  if (fd < 0)
    {
      fprintf(stderr, "npu: cannot open %s: %d\n", NPU_DEVICE, errno);
      return 1;
    }

  if (strcmp(argv[1], "info") == 0 && argc == 2)
    {
      ret = npu_info(fd);
    }
  else if (strcmp(argv[1], "selftest") == 0 && argc == 2)
    {
      ret = ioctl(fd, A733_NPUIOC_SELFTEST, 0) < 0 ? 1 : 0;
      printf("selftest: %s\n", ret == 0 ? "passed" : "failed");
    }
  else if (strcmp(argv[1], "report") == 0 && argc == 2)
    {
      ret = npu_report(fd);
    }
  else if (strcmp(argv[1], "run") == 0 && (argc == 3 || argc == 4))
    {
      ret = npu_run(fd, argv[2], argc == 4 ? argv[3] : NULL, false);
    }
  else if (strcmp(argv[1], "yolo") == 0 && argc == 3)
    {
      ret = npu_run(fd, NPU_YOLOV8_PACKAGE, argv[2], false);
    }
  else if (strcmp(argv[1], "yolo-ui") == 0 && argc == 3)
    {
      ret = npu_run(fd, NPU_YOLOV8_PACKAGE, argv[2], true);
    }
  else
    {
      npu_usage();
      ret = 1;
    }

  close(fd);
  return ret;
}
