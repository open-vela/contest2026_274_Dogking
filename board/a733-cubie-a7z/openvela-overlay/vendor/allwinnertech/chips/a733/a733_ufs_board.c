/* SPDX-License-Identifier: Apache-2.0 */
/* Independently implemented from A733 CCU register descriptions. This module
 * has NO MMIO-write/SMC/I2C/DMA callback. It cannot enable UFS, alter rails,
 * release resets, read a chip serial number, or approve a cold-start caller.
 */
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "a733_ufs_board.h"

static const uint32_t g_addresses[A733_UFS_BOARD_REGS] =
{
  0x02002000, /* PLL_REF */
  0x020020a0, /* PLL_PERI0 */
  0x020020a8, /* PLL_PERI0 pattern0 */
  0x020020ac, /* PLL_PERI0 pattern1 */
  0x020025c0, /* shared STORE AHB gate; NEVER write its key */
  0x020025e0, /* shared STORE MBUS gate */
  0x0200259c, /* shared MSI-lite1 gate/reset */
  0x02002d80, /* UFS AXI clock */
  0x02002d84, /* UFS configuration clock */
  0x02002d8c, /* UFS reset/gate */
  0x02002d90, /* UFS CCU reference enable */
  0x07090160, /* RTC oscillator selection/CLKREQ polarity */
  0x0709016c  /* RTC DCXO output gates */
};

bool a733_ufs_board_hci_allowed(void)
{
  /* Clock gates do NOT prove powered rails or domain accessibility. No
   * board-qualified PMIC provider/ownership/DMA mapping exists yet. Keep
   * this gate closed even when every diagnostic clock bit looks correct.
   * No user command, persistent file or register value can override it.
   */
  return false;
}

int a733_ufs_board_sample(struct a733_ufs_board *s,
                          a733_ufs_board_read_t read, void *context)
{
  unsigned int pass;
  unsigned int i;
  uint32_t value;
  uint32_t attempts;
  int ret;

  if (s == NULL || read == NULL)
    {
      return -EINVAL;
    }

  attempts = s->attempts;
  memset(s, 0, sizeof(*s));
  s->attempts = attempts == UINT32_MAX ? UINT32_MAX : attempts + 1;
  s->stage = "ccu-rtc-snapshot";
  /* Two fixed passes, no polling/retry. Changing snapshots stay unqualified.
   * The platform callback may read ONLY these always-on CCU/RTC registers.
   * A software timeout cannot rescue a bus read to a powered-down window.
   */
  for (pass = 0; pass < 2; pass++)
    {
      for (i = 0; i < A733_UFS_BOARD_REGS; i++)
        {
          s->last_address = g_addresses[i];
          ++s->reads;
          ret = read(context, s->last_address, &value);
          if (ret != 0)
            {
              s->stage = "snapshot-read-failed";
              s->error = ret < 0 ? ret : -EPROTO;
              return s->error;
            }

          if (pass == 0)
            {
              s->raw[i] = value;
            }
          else if (s->raw[i] != value)
            {
              s->stage = "snapshot-changing";
              s->error = -EAGAIN;
              return s->error;
            }
        }
    }

  s->stable = true;
  s->axi_mux = (s->raw[7] >> 24) & 7;
  s->cfg_mux = (s->raw[8] >> 24) & 7;
  s->axi_div = (s->raw[7] & 31) + 1;
  s->cfg_div = (s->raw[8] & 31) + 1;
  s->resets = (s->raw[9] >> 16) & 15;
  /* gate bitmap: AXI, CFG, AHB, STORE-AHB, STORE-MBUS, MSI-lite1, RTC-UFS. */
  s->gates = ((s->raw[7] >> 31) & 1) |
             (((s->raw[8] >> 31) & 1) << 1) |
             ((s->raw[9] & 1) << 2) |
             (((s->raw[4] >> 24) & 1) << 3) |
             (((s->raw[5] >> 30) & 1) << 4) |
             ((s->raw[6] & 1) << 5) | ((s->raw[12] & 1) << 6);
  switch ((s->raw[11] >> 14) & 3)
    {
      case 0:
      case 3:
        s->dcxo_hz = 24000000;
        break;
      case 1:
        s->dcxo_hz = 19200000;
        break;
      case 2:
        s->dcxo_hz = 26000000;
        break;
    }

  s->stage = "snapshot-collected-not-power-qualified";
  return 0;
}

size_t a733_ufs_board_report(const struct a733_ufs_board *s,
                             char *buffer, size_t capacity)
{
  int length;
  if (s == NULL || buffer == NULL || capacity == 0)
    {
      return 0;
    }

  length = snprintf(buffer, capacity,
    "board-v166: sampled=%u stable=%u last=%d stage=%s attempts=%" PRIu32
    " reads=%" PRIu32 " last-address=%08" PRIx32 "\n"
    "pll: ref=%08" PRIx32 " peri0=%08" PRIx32 " pat0=%08" PRIx32
    " pat1=%08" PRIx32 "\n"
    "fabric: store-ahb=%08" PRIx32 " store-mbus=%08" PRIx32
    " msi-lite1=%08" PRIx32 "\n"
    "ufs-ccu: axi=%08" PRIx32 " cfg=%08" PRIx32 " bgr=%08" PRIx32
    " ref=%08" PRIx32 "\n"
    "rtc: xo=%08" PRIx32 " xo1=%08" PRIx32 " dcxo-selected-hz=%" PRIu32
    " (selector only, NOT measured)\n"
    "decode: axi-mux=%u div=%u cfg-mux=%u div=%u reset-release-mask=%x"
    " gate-mask=%02x\n"
    "clock rates are NOT inferred from PLL names; raw values retained\n"
    "qualification: rails=unknown domain=unknown ownership=unknown"
    " dma-map=unknown hci-access=blocked\n"
    "PMIC-SMC=not-called PMIC-I2C=not-touched cold-start=blocked\n",
    s->attempts != 0, s->stable, s->error,
    s->stage == NULL ? "not-sampled" : s->stage, s->attempts, s->reads,
    s->last_address, s->raw[0], s->raw[1], s->raw[2], s->raw[3],
    s->raw[4], s->raw[5], s->raw[6], s->raw[7], s->raw[8], s->raw[9],
    s->raw[10], s->raw[11], s->raw[12], s->dcxo_hz, s->axi_mux,
    s->axi_div, s->cfg_mux, s->cfg_div, s->resets, s->gates);
  if (length < 0)
    {
      buffer[0] = 0;
      return 0;
    }

  return (size_t)length < capacity ? (size_t)length : capacity - 1;
}
