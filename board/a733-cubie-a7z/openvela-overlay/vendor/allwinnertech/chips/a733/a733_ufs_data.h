/* SPDX-License-Identifier: Apache-2.0 */
#ifndef A733_UFS_DATA_H
#define A733_UFS_DATA_H
#include "a733_ufs_scratch.h"
#include "a733_ufs_write_wire.h"
/* Pinned Kingston LUN0 partition 3, excluding its last 128 physical blocks.
 * The excluded tail contains the v172 scratch window AND both guards.
 * Never expose full-device writes or modify GPT / SD / other LUNs. */
#define A733_UFS_DATA_FIRST UINT64_C(679936)
#define A733_UFS_DATA_BLOCKS UINT64_C(30576479)
_Static_assert(A733_UFS_DATA_FIRST+A733_UFS_DATA_BLOCKS <=
               A733_UFS_SCRATCH_LBA-1,"scratch must stay outside data FS");
struct a733_ufs_data
{
  bool enabled, quarantine, existing_volume;
  int error;
  const char *stage;
  uint64_t writes, flushes, verified_bytes, read_bytes;
  uint32_t start_ms, polls, mmio;
  uint8_t descriptor[32], request[32], prdt[16];
};
/* Recovery view: qualified identity/GPT/read transport only, NO scratch and
 * NO CACHE/WRITE commands. Its block operations have no write or ioctl. */
int a733_ufs_data_ro_ready(struct a733_ufs_readcheck *,
  const struct a733_ufs_board *,uint8_t *,const struct a733_ufs_nop_io *,void *);
int a733_ufs_data_ro_read(struct a733_ufs_readcheck *,
  const struct a733_ufs_board *,uint8_t *,uint8_t *,uint64_t,uint32_t,
  const struct a733_ufs_nop_io *,void *);
/* All calls serialized by the same BSP mutex as read/scratch. work is the
 * distinct CPU-only 64KiB bench buffer; no new DMA allocations or retries. */
int a733_ufs_data_enable(struct a733_ufs_data *,struct a733_ufs_scratch *,
  struct a733_ufs_readcheck *,const struct a733_ufs_board *,uint8_t *,uint8_t *,
  const struct a733_ufs_nop_io *,void *);
/* Normal reboot path: qualify existing FAT boot+backup+FSINFO by real reads.
 * Does NOT fabricate scratch evidence, format, or issue scratch WRITEs. */
int a733_ufs_data_existing(struct a733_ufs_data *,struct a733_ufs_scratch *,
  struct a733_ufs_readcheck *,const struct a733_ufs_board *,uint8_t *,uint8_t *,
  const struct a733_ufs_nop_io *,void *);
int a733_ufs_data_io(struct a733_ufs_data *,struct a733_ufs_scratch *,
  struct a733_ufs_readcheck *,const struct a733_ufs_board *,uint8_t *,uint8_t *,
  void *,uint64_t,uint32_t,bool,const struct a733_ufs_nop_io *,void *);
int a733_ufs_data_flush(struct a733_ufs_data *,struct a733_ufs_scratch *,
  struct a733_ufs_readcheck *,const struct a733_ufs_board *,uint8_t *,uint8_t *,
  const struct a733_ufs_nop_io *,void *);
size_t a733_ufs_data_report(const struct a733_ufs_data *,char *,size_t);
#endif
