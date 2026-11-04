/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>

#ifdef CONFIG_A733_IMX214_CHECKPOINT
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <nuttx/clock.h>
#include <nuttx/fs/fs.h>
#include "arm64_internal.h"
#include "a733_camera_board.h"
#ifdef CONFIG_A733_CAMERA_CONTROL_TEST
#include "a733_camera_power_handoff.h"
#endif

#define REPORT_CAPACITY 2048

static int board_read(void *context, uint32_t address, uint32_t *value,
                       unsigned timeout_ms)
{
  (void)context;
  (void)timeout_ms;
  /* Only the fixed CCU/main PIO whitelist in a733_camera_board.c calls
   * here. These pages are already mapped as Device by the A733 BSP.
   * No CSI/ISP or unqualified bus reads, hence no module-clock enable.
   */
  *value = getreg32((uintptr_t)address);
  return 0;
}

static uint32_t board_ms(void *context)
{
  (void)context;
  return TICK2MSEC(clock_systime_ticks());
}

/* One heap report per open makes partial reads consistent and avoids a
 * shared/global snapshot racing another shell. No retained task/FD/DMA.
 */
static int camera_open(struct file *filep)
{
  static const struct a733_camera_board_io io =
    {.read = board_read, .now_ms = board_ms};
  struct a733_camera_board snapshot;
  const char *previous = filep->f_priv;
  filep->f_priv = NULL;
  char *report = calloc(1, REPORT_CAPACITY);
  if (!report) return -ENOMEM;
  /* NuttX character-device dup copies f_priv then calls open (fs_dup2.c),
   * unlike mountpoints which have a dup callback. Preserve the immutable
   * snapshot and allocate independent lifetime for the duplicated handle.
   */
  if (previous)
    {
      memcpy(report, previous, REPORT_CAPACITY);
      filep->f_priv = report;
      return 0;
    }
  int ret = a733_camera_board_sample(&snapshot, &io, NULL);
  if (ret)
    {free(report); return ret;}
  a733_camera_board_report(&snapshot, report, REPORT_CAPACITY);
  filep->f_priv = report;
  return 0;
}

static int camera_close(struct file *filep)
{
  free(filep->f_priv);
  filep->f_priv = NULL;
  return 0;
}

static ssize_t camera_read(struct file *filep, char *buffer, size_t bytes)
{
  const char *report = filep->f_priv;
  if (!report || filep->f_pos < 0) return -EINVAL;
  if (!bytes) return 0;
  if (!buffer) return -EINVAL;
  size_t length = strlen(report);
  if ((uint64_t)filep->f_pos >= length) return 0;
  size_t remaining = length - (size_t)filep->f_pos;
  if (bytes > remaining) bytes = remaining;
  memcpy(buffer, report + filep->f_pos, bytes);
  filep->f_pos += bytes;
  return bytes;
}

static const struct file_operations camera_ops =
{
  .open = camera_open,
  .close = camera_close,
  .read = camera_read
};

int a733_camera_diag_initialize(void)
{
  /* Passive registration only; no sampling or writes during boot. */
  int ret=register_driver("/dev/a733-camera", &camera_ops, 0444, NULL);
#ifdef CONFIG_A733_CAMERA_CONTROL_TEST
  if (!ret) ret=a733_camera_control_initialize();
#endif
  return ret;
}
#endif
