/* SPDX-License-Identifier: Apache-2.0 */
#ifndef A733_UFS_SCRATCH_H
#define A733_UFS_SCRATCH_H
#include "a733_ufs_readcheck.h"
#define A733_UFS_BACKUP_BYTES 65536u
struct a733_ufs_scratch
{
  bool prepared, prepare_attempted, attempted, completed, restored, quarantine;
  bool stopped, guards, submitted;
  uint32_t original_crc, elapsed_ms, writes, flushes, patterns, polls, mmio;
  uint32_t ocs, start_ms;
  uint8_t status, sense_key, asc, ascq;
  int error;
  const char *stage;
  uint8_t descriptor[32], request[32], prdt[16], response[64];
  uint8_t original[A733_UFS_BACKUP_BYTES]; /* CPU BSS; NEVER DMA. */
  uint8_t neighbors[8192];
};
/* Caller holds the BSP mutex throughout; work is a distinct CPU-only 64KiB
 * buffer. Preparation reads only. Test is one shot and may destroy disposable
 * Linux data on failure/power loss. No retries or recovery writes after error.
 * The original backup remains available in RAM, even after quarantine.
 */
int a733_ufs_scratch_prepare(struct a733_ufs_scratch *,
  struct a733_ufs_readcheck *,const struct a733_ufs_board *,uint8_t *,uint8_t *,
  const struct a733_ufs_nop_io *,void *);
int a733_ufs_scratch_test(struct a733_ufs_scratch *,
  struct a733_ufs_readcheck *,const struct a733_ufs_board *,uint8_t *,uint8_t *,
  const struct a733_ufs_nop_io *,void *);
size_t a733_ufs_scratch_report(const struct a733_ufs_scratch *,char *,size_t);
#endif
