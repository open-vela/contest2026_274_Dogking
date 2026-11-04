/* SPDX-License-Identifier: Apache-2.0 */
/* Stop empty inherited queues without resetting/rebinding the controller.
 * The write surface is exactly IE/UTRLRSR/UTMRLRSR -> zero. No memory is freed.
 */
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include "a733_ufs_quiesce.h"

static int q_fail(struct a733_ufs_quiesce *s, int error, const char *stage)
{
  s->error = error < 0 ? error : -EPROTO;
  s->stage = stage;
  s->quarantine = s->writes != 0;
  return s->error;
}

static bool q_profile(const struct a733_ufs_handoff *h)
{
  /* Match the user's v167 controller. No guessing from media capacity. */
  return h->stable && h->error == 0 && h->link_ready && !h->pending &&
         h->raw[0] == 0x11870f1f && h->raw[1] == 0x300 &&
         h->raw[2] == 0 && h->raw[3] == 0 && h->raw[6] == 1 &&
         h->raw[7] == 0 && h->raw[8] >= 0x40000000 &&
         (h->raw[8] & 1023) == 0 && h->raw[9] == 0 &&
         h->raw[12] == 0 && h->raw[13] == 0;
}

static int q_snapshot(struct a733_ufs_quiesce *s,
                      const struct a733_ufs_board *board,
                      const struct a733_ufs_quiesce_io *io, void *context,
                      uint32_t ie, uint32_t tr, uint32_t tm)
{
  unsigned int i;
  int ret = a733_ufs_handoff_snapshot(&s->after, board, io->read, context);
  if (ret != 0)
    {
      return q_fail(s, ret, "quiesce-snapshot-failed-no-retry");
    }

  if (!q_profile(&s->after) || s->after.raw[4] != ie ||
      s->after.raw[11] != tr || s->after.raw[15] != tm)
    {
      return q_fail(s, -EBUSY, "quiesce-state-mismatch-no-retry");
    }

  /* Everything except our three controls must remain identical, including
   * original firmware queue bases. This does NOT free their backing memory.
   */
  for (i = 0; i < A733_UFS_HANDOFF_REGS; i++)
    {
      if (i != 4 && i != 11 && i != 15 &&
          s->after.raw[i] != s->before.raw[i])
        {
          return q_fail(s, -EAGAIN, "quiesce-unowned-state-changed");
        }
    }

  return 0;
}

int a733_ufs_quiesce_run(struct a733_ufs_quiesce *s,
                        const struct a733_ufs_board *board,
                        const struct a733_ufs_quiesce_io *io, void *context)
{
  static const uint32_t controls[] = { 0x24, 0x60, 0x80 };
  uint32_t ie;
  uint32_t tr;
  uint32_t tm;
  uint32_t start;
  uint32_t value;
  unsigned int i;
  unsigned int poll;
  int ret;
  if (s == NULL || io == NULL || io->read == NULL ||
      io->write_zero == NULL || io->now_ms == NULL ||
      io->delay_100us == NULL)
    {
      return -EINVAL;
    }

  if (s->attempted)
    {
      return -EALREADY; /* Preserve the original result, no extra MMIO. */
    }

  s->attempted = true;
  s->stage = "quiesce-preflight";
  ret = a733_ufs_handoff_snapshot(&s->before, board, io->read, context);
  if (ret != 0)
    {
      return q_fail(s, ret, "quiesce-preflight-failed");
    }

  s->after = s->before;
  if (!q_profile(&s->before))
    {
      return q_fail(s, -EXDEV, "quiesce-controller-profile-required");
    }

  ie = s->before.raw[4];
  tr = s->before.raw[11];
  tm = s->before.raw[15];
  if (ie == 0 && tr == 0 && tm == 0)
    {
      s->completed = true;
      s->stage = "quiesce-already-stopped-no-dma-authority";
      return 0;
    }

  if (ie != 0x30e15 || tr != 1 || tm != 1)
    {
      return q_fail(s, -EXDEV, "quiesce-firmware-controls-unrecognized");
    }

  for (i = 0; i < sizeof(controls) / sizeof(controls[0]); i++)
    {
      /* Fresh stable snapshot immediately before EACH write. The board
       * adapter also rechecks its always-on gates for every MMIO access.
       */
      ret = q_snapshot(s, board, io, context, ie, tr, tm);
      if (ret != 0) return ret;
      s->stage = "quiesce-zero-control";
      s->last_offset = controls[i];
      ++s->writes;
      ret = io->write_zero(context, controls[i]);
      if (ret != 0) return q_fail(s, ret, "quiesce-write-failed");
      start = io->now_ms(context);
      for (poll = 0; poll < A733_UFS_QUIESCE_POLLS; poll++)
        {
          ++s->polls;
          ret = io->read(context, controls[i], &value);
          if (ret != 0) return q_fail(s, ret, "quiesce-readback-failed");
          s->elapsed_ms = io->now_ms(context) - start;
          if (s->elapsed_ms > A733_UFS_QUIESCE_MS)
            return q_fail(s, -ETIMEDOUT, "quiesce-deadline-expired");
          if (value == 0) break;
          io->delay_100us(context);
        }

      if (poll == A733_UFS_QUIESCE_POLLS)
        return q_fail(s, -ETIMEDOUT, "quiesce-poll-bound-expired");
      if (i == 0) ie = 0;
      else if (i == 1) tr = 0;
      else tm = 0;
      ret = q_snapshot(s, board, io, context, ie, tr, tm);
      if (ret != 0) return ret;
    }

  s->completed = true;
  s->stage = "quiesce-controls-stopped-not-dma-authorized";
  return 0;
}

size_t a733_ufs_quiesce_report(const struct a733_ufs_quiesce *s,
                              char *buffer, size_t capacity)
{
  int n;
  if (s == NULL || buffer == NULL || capacity == 0) return 0;
  n = snprintf(buffer, capacity,
    "quiesce-v168: attempted=%u completed=%u quarantine=%u last=%d"
    " stage=%s writes=%" PRIu32 " polls=%" PRIu32
    " last-offset=%02" PRIx32 " last-wait-ms=%" PRIu32 "\n"
    "quiesce-before: ie=%08" PRIx32 " utrl-rsr=%08" PRIx32
    " utmr-rsr=%08" PRIx32 " utrl-base=%08" PRIx32 "/%08" PRIx32 "\n"
    "quiesce-after: ie=%08" PRIx32 " utrl-rsr=%08" PRIx32
    " utmr-rsr=%08" PRIx32 " utrl-base=%08" PRIx32 "/%08" PRIx32 "\n"
    "quiesce-policy: zero-only IE/UTRLRSR/UTMRLRSR; no reset/restart;"
    " no doorbell/IS/base writes; DMA/attach blocked; reboot to retry\n",
    s->attempted, s->completed, s->quarantine, s->error,
    s->stage == NULL ? "not-attempted" : s->stage,
    s->writes, s->polls, s->last_offset, s->elapsed_ms,
    s->before.raw[4], s->before.raw[11], s->before.raw[15],
    s->before.raw[8], s->before.raw[9], s->after.raw[4],
    s->after.raw[11], s->after.raw[15], s->after.raw[8], s->after.raw[9]);
  if (n < 0) {buffer[0] = 0; return 0;}
  return (size_t)n < capacity ? (size_t)n : capacity - 1;
}
