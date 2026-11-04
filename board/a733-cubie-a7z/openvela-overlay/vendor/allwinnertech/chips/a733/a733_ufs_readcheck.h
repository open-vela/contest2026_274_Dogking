/* SPDX-License-Identifier: Apache-2.0 */
#ifndef A733_UFS_READCHECK_H
#define A733_UFS_READCHECK_H
#include "a733_ufs_nop.h"
#define A733_UFS_READ_SAMPLE 16384u
struct a733_ufs_readcheck
{
  struct a733_ufs_handoff after;
  uint64_t physical, blocks, readbytes, lbas[3], entry_lba;
  uint64_t first_usable, last_usable, partitions[4][2];
  uint32_t blocksize, pages, writes, polls, commands, samples, elapsed_ms, ocs;
  uint32_t crc[3], repeat_crc[3], entry_bytes, entry_count, entry_size, entry_crc;
  uint32_t gpt_partitions, ua_retries, exceptions;
  uint64_t reader_calls, reader_bytes;
  uint32_t reader_budget, reader_elapsed_ms;
  bool reader_active;
  bool attempted, published, completed, stopped, guards, quarantine, identity;
  bool protective_mbr, gpt_present, gpt_header_valid, gpt_entries_valid;
  uint8_t lun, opcode, scsi_status, sense_key, asc, ascq;
  int error, gpt_error;
  const char *stage;
  char model[29];
  uint8_t descriptor[32], request[32], prdt[16], response[64];
  uint8_t reference[A733_UFS_READ_SAMPLE]; /* CPU only, never DMA */
};
/* One explicit batch after a successful NOP in this boot. A NEW dedicated
 * guarded arena (not the NOP arena), retained forever. No block registration,
 * no media writes/FS/reset. One well-framed initial UNIT ATTENTION may be
 * consumed/reissued before data reads; NEVER retry a transport failure.
 */
int a733_ufs_readcheck_run(struct a733_ufs_readcheck *,
                          const struct a733_ufs_board *,
                          const struct a733_ufs_nop *, uint8_t *, uint8_t lun,
                          const struct a733_ufs_nop_io *, void *);
size_t a733_ufs_readcheck_report(const struct a733_ufs_readcheck *,char *,size_t);
/* Caller serializes the same arena/state. Requires this boot's successful
 * readcheck, fresh stopped HCI, and the original static identity mapping.
 * Up to 1MiB per call, 64KiB per command. Read-only; no retry/free/reset.
 * On error the destination may have a completed prefix; never publish it.
 */
int a733_ufs_reader_ready(struct a733_ufs_readcheck *,
                        const struct a733_ufs_board *,uint8_t *,
                        const struct a733_ufs_nop_io *,void *);
int a733_ufs_reader_read(struct a733_ufs_readcheck *,
                        const struct a733_ufs_board *,uint8_t *,uint8_t *,
                        uint64_t,uint32_t,const struct a733_ufs_nop_io *,void *);
#endif
