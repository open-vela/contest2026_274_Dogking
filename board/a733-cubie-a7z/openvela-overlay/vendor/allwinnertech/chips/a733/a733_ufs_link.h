/* SPDX-License-Identifier: Apache-2.0 */
#ifndef A733_UFS_LINK_H
#define A733_UFS_LINK_H

#include <stdbool.h>
#include <stdint.h>

/* Internal board contract, NOT shell arguments or a persisted permission token.
 * The current BSP has no qualified implementation of this board contract.
 * In particular, clocks ready != rails/domain ready. Never synthesize proofs
 * from user-entered voltages or DT regulator min/max values.
 */
#define A733_UFS_LINK_PROOFS UINT32_C(0x1ff)
#define A733_UFS_PROOF_RAILS       UINT32_C(0x01)
#define A733_UFS_PROOF_DOMAIN      UINT32_C(0x02)
#define A733_UFS_PROOF_CLOCKS      UINT32_C(0x04)
#define A733_UFS_PROOF_OWNER       UINT32_C(0x08)
#define A733_UFS_PROOF_SOC         UINT32_C(0x10)
#define A733_UFS_PROOF_REFCLK      UINT32_C(0x20)
#define A733_UFS_PROOF_PHY_HELD    UINT32_C(0x40)
#define A733_UFS_PROOF_CALIBRATION UINT32_C(0x80)
#define A733_UFS_PROOF_DEVICE_RESET UINT32_C(0x100)

struct a733_ufs_link_board
{
  uint32_t proofs;
  uint32_t cal_low;
  uint32_t cal_high;
};

struct a733_ufs_link_io
{
  /* All int callbacks: zero on success, negative errno on failure. qualify is
   * read-only and must reject unknown/incomplete board state before HCI reads.
   * MMIO callbacks are non-blocking, fence is an actual device-ordering DSB on
   * AArch64, now_ms is a wrapping monotonic clock. No callbacks may cancel the
   * caller or release serialization during the transaction. A power-gated bus
   * hang cannot be made safe by a software polling timeout.
   */
  void *context;
  int (*qualify)(void *context, struct a733_ufs_link_board *board);
  int (*read)(void *context, unsigned int offset, uint32_t *value);
  int (*write)(void *context, unsigned int offset, uint32_t value);
  int (*release_phy)(void *context);
  void (*fence)(void *context);
  void (*delay_us)(void *context, unsigned int us);
  uint32_t (*now_ms)(void *context);
};

enum a733_ufs_link_phase
{
  A733_UFS_LINK_IDLE,
  A733_UFS_LINK_BOARD,
  A733_UFS_LINK_HOST,
  A733_UFS_LINK_RMMI,
  A733_UFS_LINK_SRAM,
  A733_UFS_LINK_CAL,
  A733_UFS_LINK_ENABLE,
  A733_UFS_LINK_START,
  A733_UFS_LINK_VERIFY,
  A733_UFS_LINK_CPORT,
  A733_UFS_LINK_UP
};

struct a733_ufs_link_state
{
  enum a733_ufs_link_phase phase;
  bool attempted;
  bool quarantined;
  bool link_up;
  int error;
  uint32_t start_ms;
  uint32_t elapsed_ms;
  uint32_t commands;
  uint32_t polls;
  uint32_t delay_total_us;
  uint32_t last_offset;
  uint32_t last_value;
  uint32_t last_attribute;
  uint32_t last_uic_result;
};

/* Caller owns serialization for the entire call and keeps state until cold
 * power-cycle; no clear-quarantine/retry API. Zero-initialize once at boot.
 * One link attempt, no DMA, UTRL, UPIU, SCSI, media writes or HS gear change.
 * Success means link/cport only, NOT NOP/device-init/LUN/storage acceptance.
 */
int a733_ufs_link_start(struct a733_ufs_link_state *state,
                        const struct a733_ufs_link_io *io);

#endif
