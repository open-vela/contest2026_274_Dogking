/* SPDX-License-Identifier: Apache-2.0 */
/* Project-owned byte encoders, not Linux structs/bitfields. All wire offsets
 * are UFSHCI/UPIU/SCSI ABI values. See docs/a733/UFS_STORAGE.md for provenance.
 */
#ifndef A733_UFS_WIRE_H
#define A733_UFS_WIRE_H

#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <limits.h>

#define A733_UFS_TRANSFER_MAX 65536u
#define A733_UFS_DMA_SIZE     (4096u + A733_UFS_TRANSFER_MAX)
#define A733_UFS_UCD_OFFSET   1024u
#define A733_UFS_RSP_OFFSET   1536u
#define A733_UFS_PRD_OFFSET   2048u
#define A733_UFS_DATA_OFFSET  4096u

static inline void ufs_le32(uint8_t *p, uint32_t n)
{
  p[0] = n; p[1] = n >> 8; p[2] = n >> 16; p[3] = n >> 24;
}

static inline uint32_t ufs_getle32(const uint8_t *p)
{
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
         (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static inline void ufs_be32(uint8_t *p, uint32_t n)
{
  p[0] = n >> 24; p[1] = n >> 16; p[2] = n >> 8; p[3] = n;
}

static inline uint32_t ufs_getbe32(const uint8_t *p)
{
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
         (uint32_t)p[2] << 8 | p[3];
}

static inline uint64_t ufs_getbe64(const uint8_t *p)
{
  return (uint64_t)ufs_getbe32(p) << 32 | ufs_getbe32(p + 4);
}

/* Exactly the small read-only command set implemented by this driver. There
 * is intentionally no raw CDB, host-to-device data, FORMAT, UNMAP or WRITE.
 */
static inline int ufs_read_command(uint8_t *memory, uint64_t physical,
                                  uint8_t lun, uint8_t opcode,
                                  uint64_t lba, uint32_t count,
                                  uint32_t blocksize, uint8_t tag)
{
  uint8_t *cmd;
  uint8_t *cdb;
  uint32_t bytes = 0;
  uint64_t ucd;
  uint64_t data;

  if (!memory || lun > 7 || (physical & 1023) ||
      physical > UINT64_MAX - A733_UFS_DMA_SIZE)
    {
      return -EINVAL;
    }

  switch (opcode)
    {
      case 0: break;                 /* NOP OUT, not TEST UNIT READY */
      case 0x12: bytes = 36; break;   /* INQUIRY */
      case 0x9e: bytes = 32; break;   /* READ CAPACITY(16) */
      case 0x88:                     /* READ(16) */
        if ((blocksize != 512 && blocksize != 4096) || !count ||
            count > A733_UFS_TRANSFER_MAX / blocksize ||
            lba > UINT64_MAX - count)
          {
            return -EINVAL;
          }

        bytes = count * blocksize;
        break;
      default: return -EPERM;
    }

  cmd = memory + A733_UFS_UCD_OFFSET;
  cdb = cmd + 16;
  memset(memory, 0, A733_UFS_DMA_SIZE);
  ucd = physical + A733_UFS_UCD_OFFSET;
  data = physical + A733_UFS_DATA_OFFSET;
  /* UTRD: UFS-storage CT, interrupt requested, optional device-to-host. */
  ufs_le32(memory, UINT32_C(0x11000000) | (bytes ? 0x04000000 : 0));
  ufs_le32(memory + 8, 15);          /* OCS invalid until hardware completes */
  ufs_le32(memory + 16, (uint32_t)ucd);
  ufs_le32(memory + 20, (uint32_t)(ucd >> 32));
  ufs_le32(memory + 24, 128u | (128u << 16)); /* response: 512B at UCD+512 */
  ufs_le32(memory + 28, (bytes ? 1u : 0u) | (256u << 16));
  cmd[0] = opcode ? 1 : 0;
  cmd[1] = bytes ? 0x40 : 0;
  cmd[2] = opcode ? lun : 0;
  cmd[3] = tag;
  ufs_be32(cmd + 12, bytes);
  cdb[0] = opcode;
  if (opcode == 0x12)
    {
      cdb[4] = 36;
    }
  else if (opcode == 0x9e)
    {
      cdb[1] = 0x10;
      ufs_be32(cdb + 10, bytes);
    }
  else if (opcode == 0x88)
    {
      ufs_be32(cdb + 2, (uint32_t)(lba >> 32));
      ufs_be32(cdb + 6, (uint32_t)lba);
      ufs_be32(cdb + 10, count);
    }

  if (bytes)
    {
      uint8_t *prd = memory + A733_UFS_PRD_OFFSET;
      ufs_le32(prd, (uint32_t)data);
      ufs_le32(prd + 4, (uint32_t)(data >> 32));
      ufs_le32(prd + 12, bytes - 1);
    }

  return (int)bytes;
}

static inline int ufs_read_response(const uint8_t *memory, uint8_t lun,
                                   uint8_t opcode, uint8_t tag)
{
  const uint8_t *rsp = memory + A733_UFS_RSP_OFFSET;
  if ((ufs_getle32(memory + 8) & 15) != 0 ||
      rsp[0] != (opcode ? 0x21 : 0x20) || rsp[3] != tag ||
      rsp[2] != (opcode ? lun : 0) || rsp[6] || rsp[7])
    {
      return -EIO;
    }

  /* Reject overflow/underflow and all residuals, not partial-success reads.
   * Basic NOP response is 12 bytes, no command-specific fields to inspect.
   */
  if (opcode && ((rsp[1] & 0x60) || ufs_getbe32(rsp + 12)))
    {
      return -EIO;
    }

  return 0;
}

static inline int ufs_read_capacity(const uint8_t *data, uint64_t *blocks,
                                   uint32_t *blocksize)
{
  uint64_t last = ufs_getbe64(data);
  uint32_t size = ufs_getbe32(data + 8);
  if (last == UINT64_MAX || last >= INT64_MAX ||
      (size != 512 && size != 4096) || last + 1 > INT64_MAX / size)
    {
      return -EOVERFLOW;
    }

  *blocks = last + 1;
  *blocksize = size;
  return 0;
}

static inline int ufs_read_range(uint64_t lba, uint64_t count,
                                uint64_t capacity)
{
  return lba > capacity || count > capacity - lba ? -EINVAL : 0;
}

#endif
