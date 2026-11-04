/* SPDX-License-Identifier: Apache-2.0 */
/* Private scratch ABI. The public read encoder deliberately remains READ-only.
 * Primary ABI: vendor Linux scsi_proto.h / ufshcd.c and U-Boot drivers/ufs/ufs.h.
 */
#ifndef A733_UFS_WRITE_WIRE_H
#define A733_UFS_WRITE_WIRE_H
#include <stdbool.h>
#include "a733_ufs_wire.h"
#define A733_UFS_SCRATCH_LBA UINT64_C(31256496)
#define A733_UFS_SCRATCH_BYTES 65536u
static inline int ufs_scratch_command(uint8_t *m,uint64_t pa,bool flush)
{
  if(!m||(pa&4095)||pa>UINT64_MAX-A733_UFS_DMA_SIZE)return -EINVAL;
  memset(m,0,A733_UFS_DMA_SIZE);
  uint64_t ucd=pa+A733_UFS_UCD_OFFSET,data=pa+A733_UFS_DATA_OFFSET;
  ufs_le32(m,flush?0x11000000:0x13000000);
  ufs_le32(m+8,15);ufs_le32(m+16,(uint32_t)ucd);ufs_le32(m+20,ucd>>32);
  ufs_le32(m+24,128u|(128u<<16));
  ufs_le32(m+28,(flush?0u:1u)|(256u<<16));
  uint8_t *cmd=m+A733_UFS_UCD_OFFSET,*cdb=cmd+16;
  cmd[0]=1;cmd[1]=flush?0:0x20; /* LUN0, tag0, SIMPLE; no IMMED/FUA. */
  ufs_be32(cmd+12,flush?0:A733_UFS_SCRATCH_BYTES);
  cdb[0]=flush?0x35:0x8a;
  if(!flush)
    {
      ufs_be32(cdb+2,A733_UFS_SCRATCH_LBA>>32);
      ufs_be32(cdb+6,(uint32_t)A733_UFS_SCRATCH_LBA);
      ufs_be32(cdb+10,16);
      uint8_t *prd=m+A733_UFS_PRD_OFFSET;
      ufs_le32(prd,(uint32_t)data);ufs_le32(prd+4,data>>32);
      ufs_le32(prd+12,A733_UFS_SCRATCH_BYTES-1);
    }
  /* Vendor sd_sync_cache/scsi_setup_sync_cache use CACHE(10), not CACHE(16).
   * LBA=0 / count=0 flushes this LUN, no IMMED, no data PRDT. */
  return 0;
}
#endif
