/* SPDX-License-Identifier: Apache-2.0 */
#ifndef A733_UFS_NOP_H
#define A733_UFS_NOP_H
#include "a733_ufs_quiesce.h"
#include "a733_ufs_wire.h"
#define A733_UFS_NOP_GUARD 4096u
#define A733_UFS_NOP_ARENA (A733_UFS_DMA_SIZE + 2u * A733_UFS_NOP_GUARD)
struct a733_ufs_nop
{
  struct a733_ufs_handoff before, after;
  uint64_t physical;
  uint32_t pages, writes, polls, last_offset, wait_ms;
  bool attempted, published, submitted, completed, stopped, guards, quarantine;
  int error;
  const char *stage;
  uint8_t descriptor[32], request[32], response[32];
};
struct a733_ufs_nop_io
{
  a733_ufs_board_read_t read;
  int (*write)(void *, uint32_t offset, uint32_t value);
  int (*translate)(void *, uintptr_t virtual_address, uint64_t *physical);
  void (*clean)(void *, uintptr_t address, size_t size);
  void (*invalidate)(void *, uintptr_t address, size_t size);
  uint32_t (*now_ms)(void *);
  void (*delay_100us)(void *);
};
/* Zero-initialized state; dedicated page-aligned arena, never released or
 * reused until reboot, even on success. Only one NOP OUT, never a SCSI CDB.
 */
int a733_ufs_nop_run(struct a733_ufs_nop *state,
                    const struct a733_ufs_board *board,
                    const struct a733_ufs_quiesce *quiesce,
                    uint8_t *arena, const struct a733_ufs_nop_io *io, void *ctx);
size_t a733_ufs_nop_report(const struct a733_ufs_nop *, char *, size_t);
/* ARM64 platform implementation: current EL1 translation R+W, IRQ protected. */
int a733_ufs_nop_translate(void *, uintptr_t virtual_address, uint64_t *physical);
#endif
