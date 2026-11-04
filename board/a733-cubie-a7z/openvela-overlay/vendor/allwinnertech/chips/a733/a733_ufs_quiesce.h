/* SPDX-License-Identifier: Apache-2.0 */
#ifndef A733_UFS_QUIESCE_H
#define A733_UFS_QUIESCE_H
#include "a733_ufs_handoff.h"

#define A733_UFS_QUIESCE_POLLS 200
#define A733_UFS_QUIESCE_MS 20
struct a733_ufs_quiesce
{
  struct a733_ufs_handoff before;
  struct a733_ufs_handoff after;
  uint32_t writes; /* Attempted: an error may still have reached the device. */
  uint32_t polls;
  uint32_t elapsed_ms;
  uint32_t last_offset;
  bool attempted;
  bool completed;
  bool quarantine;
  int error;
  const char *stage;
};
struct a733_ufs_quiesce_io
{
  a733_ufs_board_read_t read;
  int (*write_zero)(void *context, uint32_t offset);
  uint32_t (*now_ms)(void *context);
  void (*delay_100us)(void *context);
};

/* One manual attempt per boot, no reset/restart/base/doorbell/IS writes.
 * Success means IE/runstop read back zero, NOT DMA ownership qualification.
 * write_zero must order and complete its MMIO store before returning.
 */
int a733_ufs_quiesce_run(struct a733_ufs_quiesce *state,
                        const struct a733_ufs_board *board,
                        const struct a733_ufs_quiesce_io *io, void *context);
size_t a733_ufs_quiesce_report(const struct a733_ufs_quiesce *state,
                              char *buffer, size_t capacity);
#endif
