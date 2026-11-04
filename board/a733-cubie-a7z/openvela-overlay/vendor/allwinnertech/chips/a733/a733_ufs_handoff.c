/* SPDX-License-Identifier: Apache-2.0 */
/* Read-only observation of an inherited, firmware-initialized HCI window.
 * No write callback, no queue stop/clear, reset, DMA allocation or command.
 * This is NOT a generic power provider and NEVER authorizes DMA/media access.
 */
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "a733_ufs_handoff.h"

static const uint32_t g_offsets[A733_UFS_HANDOFF_REGS] =
{
  0x00, 0x08, 0x18, 0x20, 0x24, 0x30, 0x34, 0x4c,
  0x50, 0x54, 0x58, 0x60, 0x70, 0x74, 0x78, 0x80
};

bool a733_ufs_handoff_profile(const struct a733_ufs_board *s)
{
  /* Deliberately narrow: match the CCU/RTC profile observed on the user's
   * v166 boot that reported successful U-Boot UFS enumeration. This is NOT
   * proof of rail voltages or firmware identity. Board testing must retain
   * that boot contract; unknown profiles refuse before touching the HCI.
   */
  return s != NULL && s->stable && s->error == 0 && s->reads == 26 &&
         s->gates == 0x7f && s->resets == 15 && s->dcxo_hz == 26000000 &&
         s->raw[0] == UINT32_C(0xf8675f00) &&
         s->raw[1] == UINT32_C(0xff126310) &&
         (s->raw[6] & 0x30001) == 0x30001 &&
         s->raw[7] == UINT32_C(0x81000000) &&
         s->raw[8] == UINT32_C(0x80000018) &&
         (s->raw[9] & 0xf0001) == 0xf0001 &&
         (s->raw[11] & UINT32_C(0x8000c000)) == 0x8000 &&
         (s->raw[12] & 1) != 0;
}

int a733_ufs_handoff_snapshot(struct a733_ufs_handoff *s,
                              const struct a733_ufs_board *board,
                              a733_ufs_board_read_t read, void *context)
{
  uint32_t value;
  unsigned int pass;
  unsigned int i;
  int ret;
  if (s == NULL || read == NULL)
    {
      return -EINVAL;
    }

  memset(s, 0, sizeof(*s));
  s->stage = "handoff-profile-required";
  if (!a733_ufs_handoff_profile(board))
    {
      s->error = -EXDEV;
      return s->error;
    }

  s->stage = "handoff-reading-only";
  for (pass = 0; pass < 2; pass++)
    {
      for (i = 0; i < A733_UFS_HANDOFF_REGS; i++)
        {
          s->last_offset = g_offsets[i];
          ++s->reads;
          ret = read(context, s->last_offset, &value);
          if (ret != 0)
            {
              s->stage = "handoff-read-failed";
              s->error = ret < 0 ? ret : -EPROTO;
              return s->error;
            }

          if (pass == 0)
            {
              s->raw[i] = value;
            }
          else if (s->raw[i] != value)
            {
              s->stage = "handoff-changing-no-retry";
              s->error = -EAGAIN;
              return s->error;
            }
        }
    }

  s->stable = true;
  if (s->raw[0] == UINT32_MAX || s->raw[1] < 0x200 ||
      s->raw[1] > 0x310 || s->raw[6] != 1)
    {
      s->stage = "handoff-invalid-controller";
      s->error = -ENOLINK;
      return s->error;
    }

  if (s->raw[3] & UINT32_C(0x00070884))
    {
      s->stage = "handoff-controller-error-no-clear";
      s->error = -EIO;
      return s->error;
    }

  s->link_ready = (s->raw[5] & 0x33) == 3;
  s->pending = s->raw[10] != 0 || s->raw[14] != 0;
  s->needs_quiesce = s->pending || s->raw[2] != 0 || s->raw[4] != 0 ||
                      s->raw[11] != 0 || s->raw[15] != 0;
  s->stage = "handoff-observed-not-dma-authorized";
  return 0;
}

size_t a733_ufs_handoff_report(const struct a733_ufs_handoff *s,
                               char *buffer, size_t capacity)
{
  int n;
  if (s == NULL || buffer == NULL || capacity == 0)
    {
      return 0;
    }

  n = snprintf(buffer, capacity,
    "handoff-v167: stage=%s last=%d stable=%u reads=%" PRIu32
    " last-offset=%02" PRIx32 " link-ready=%u pending=%u needs-quiesce=%u\n"
    "hci: cap=%08" PRIx32 " ver=%08" PRIx32 " ahit=%08" PRIx32
    " is=%08" PRIx32 " ie=%08" PRIx32 " hcs=%08" PRIx32
    " hce=%08" PRIx32 " iac=%08" PRIx32 "\n"
    "utrl: base=%08" PRIx32 "/%08" PRIx32 " doorbell=%08" PRIx32
    " runstop=%08" PRIx32 "\n"
    "utmr: base=%08" PRIx32 "/%08" PRIx32 " doorbell=%08" PRIx32
    " runstop=%08" PRIx32 "\n"
    "handoff-policy: firmware-powered assumption; no PMIC qualification;"
    " register-writes=0 dma=0 media-commands=0 attach-blocked=1\n",
    s->stage == NULL ? "not-sampled" : s->stage, s->error, s->stable,
    s->reads, s->last_offset, s->link_ready, s->pending, s->needs_quiesce,
    s->raw[0], s->raw[1], s->raw[2], s->raw[3], s->raw[4], s->raw[5],
    s->raw[6], s->raw[7], s->raw[8], s->raw[9], s->raw[10], s->raw[11],
    s->raw[12], s->raw[13], s->raw[14], s->raw[15]);
  if (n < 0)
    {
      buffer[0] = 0;
      return 0;
    }

  return (size_t)n < capacity ? (size_t)n : capacity - 1;
}
