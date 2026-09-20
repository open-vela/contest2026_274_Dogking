/****************************************************************************
 * vendor/rockchip/chips/rv1103/rv1103_sfc_nand.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_RV1103_SFC_NAND

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/fs/fs.h>
#include <nuttx/fs/ioctl.h>
#include <nuttx/mtd/mtd.h>
#include <nuttx/mutex.h>

#include <arch/chip/rv1103_sfc_nand.h>

#include "arm_internal.h"

/* Hardware data is taken from the official RV1103/RV1106 DTS, CRU driver,
 * Rockchip rkflash SFC implementation and its EF AE 21 device table.
 */

#define RV1103_SFC_BASE                 0xffac0000u

#define RV1103_PERI_CLKGATE_CON4        0xff3b2810u
#define RV1103_PERI_CLKGATE_CON5        0xff3b2814u
#define RV1103_PERI_SOFTRST_CON4        0xff3b2a10u
#define RV1103_PERI_SOFTRST_CON5        0xff3b2a14u
#define RV1103_HCLK_SFC_BIT             (1u << 14)
#define RV1103_SCLK_SFC_BIT             (1u << 0)

#define SFC_CTRL                         0x000u
#define SFC_IMR                          0x004u
#define SFC_ICLR                         0x008u
#define SFC_RCVR                         0x010u
#define SFC_ABIT                         0x018u
#define SFC_FSR                          0x020u
#define SFC_SR                           0x024u
#define SFC_VER                          0x02cu
#define SFC_LEN_CTRL                     0x088u
#define SFC_LEN_EXT                      0x08cu
#define SFC_CMD                          0x100u
#define SFC_ADDR                         0x104u
#define SFC_DATA                         0x108u

#define SFC_RXEMPTY                      (1u << 2)
#define SFC_TXEMPTY                      (1u << 0)
#define SFC_BUSY                         (1u << 0)
#define SFC_RESET                        (1u << 0)

#define SFC_CMD_RW                       (1u << 12)
#define SFC_CMD_ADDR24                   (1u << 14)
#define SFC_CMD_ADDRX                    (3u << 14)
#define SFC_CTRL_SPS                     (1u << 1)
#define SFC_CTRL_DATALINES_X1            (0u << 12)
#define SFC_CTRL_ADDRBITS(n)             (((uint32_t)(n) - 1u) << 16)

#define NAND_CMD_READ_ID                 0x9fu
#define NAND_CMD_WRITE_ENABLE            0x06u
#define NAND_CMD_GET_FEATURE             0x0fu
#define NAND_CMD_SET_FEATURE             0x1fu
#define NAND_CMD_PAGE_READ               0x13u
#define NAND_CMD_READ_CACHE              0x03u
#define NAND_CMD_PROGRAM_LOAD             0x02u
#define NAND_CMD_PROGRAM_EXECUTE          0x10u
#define NAND_CMD_BLOCK_ERASE              0xd8u
#define NAND_FEATURE_PROTECTION           0xa0u
#define NAND_FEATURE_STATUS              0xc0u
#define NAND_STATUS_BUSY                 (1u << 0)
#define NAND_STATUS_WRITE_ENABLE          (1u << 1)
#define NAND_STATUS_ERASE_FAIL            (1u << 2)
#define NAND_STATUS_PROGRAM_FAIL          (1u << 3)
#define NAND_STATUS_ECC_SHIFT            4u
#define NAND_STATUS_ECC_MASK             3u

#define NAND_ID0                         0xefu
#define NAND_ID1                         0xaeu
#define NAND_ID2                         0x21u
#define NAND_PAGE_SIZE                   2048u
#define NAND_OOB_SIZE                    64u
#define NAND_PROGRAM_SIZE                (NAND_PAGE_SIZE + NAND_OOB_SIZE)
#define NAND_PAGES_PER_BLOCK             64u
#define NAND_ERASE_SIZE                  (NAND_PAGE_SIZE * NAND_PAGES_PER_BLOCK)
#define NAND_ERASE_BLOCKS                1024u
#define NAND_TOTAL_SIZE                  (NAND_ERASE_SIZE * NAND_ERASE_BLOCKS)

#define NAND_BOOT_OFFSET                 (1u * 1024u * 1024u)
#define NAND_USERDATA_OFFSET             0x02300000u
#define NAND_USERDATA_SIZE               (6u * 1024u * 1024u)
#define NAND_TRANSFER_TIMEOUT_US         100000u
#define NAND_READY_TIMEOUT_US            1000000u

struct rv1103_nand_partition_s
{
  const char *path;
  const char *name;
  uint32_t offset;
  uint32_t size;
  bool writable;
};

struct rv1103_nand_s
{
  struct mtd_dev_s mtd;
  mutex_t lock;
  uint16_t version;
  uint8_t id[3];
  bool initialized;
};

static int rv1103_nand_erase(struct mtd_dev_s *dev, off_t startblock,
                             size_t nblocks);
static ssize_t rv1103_nand_bread(struct mtd_dev_s *dev, off_t startblock,
                                 size_t nblocks, uint8_t *buffer);
static ssize_t rv1103_nand_bwrite(struct mtd_dev_s *dev, off_t startblock,
                                  size_t nblocks, const uint8_t *buffer);
static ssize_t rv1103_nand_read(struct mtd_dev_s *dev, off_t offset,
                                size_t nbytes, uint8_t *buffer);
static int rv1103_nand_ioctl(struct mtd_dev_s *dev, int cmd,
                             unsigned long arg);
static int rv1103_nand_isbad(struct mtd_dev_s *dev, off_t block);
static int rv1103_nand_markbad(struct mtd_dev_s *dev, off_t block);

static struct rv1103_nand_s g_nand =
{
  .mtd =
  {
    .erase   = rv1103_nand_erase,
    .bread   = rv1103_nand_bread,
    .bwrite  = rv1103_nand_bwrite,
    .read    = rv1103_nand_read,
    .ioctl   = rv1103_nand_ioctl,
    .isbad   = rv1103_nand_isbad,
    .markbad = rv1103_nand_markbad,
    .name    = "rv1103-spinand",
  },
  .lock = NXMUTEX_INITIALIZER,
};

static uint8_t g_program_buffer[NAND_PROGRAM_SIZE];
static uint8_t g_verify_buffer[NAND_PAGE_SIZE];

static const struct rv1103_nand_partition_s g_partitions[] =
{
  {"/dev/nand-env",      "env",       0x00000000u,  256u * 1024u, false},
  {"/dev/nand-idblock",  "idblock",   0x00040000u,  256u * 1024u, false},
  {"/dev/nand-uboot",    "uboot",     0x00080000u,  512u * 1024u, false},
  {"/dev/nand-boot",     "boot",      0x00100000u,    4u * 1024u * 1024u,
   false},
  {"/dev/nand-oem",      "oem",       0x00500000u,   30u * 1024u * 1024u,
   false},
  {"/dev/nand-userdata", "userdata",  NAND_USERDATA_OFFSET,
   NAND_USERDATA_SIZE, true},
  {"/dev/nand-rootfs",   "rootfs",    0x02900000u,   85u * 1024u * 1024u,
   false},
};

static inline uint32_t rv1103_sfc_getreg(uint32_t offset)
{
  return getreg32(RV1103_SFC_BASE + offset);
}

static inline void rv1103_sfc_putreg(uint32_t value, uint32_t offset)
{
  putreg32(value, RV1103_SFC_BASE + offset);
}

static int rv1103_sfc_wait_idle(uint32_t timeout)
{
  while ((rv1103_sfc_getreg(SFC_SR) & SFC_BUSY) != 0)
    {
      if (timeout-- == 0)
        {
          return -ETIMEDOUT;
        }

      up_udelay(1);
    }

  return 0;
}

static int rv1103_sfc_reset(void)
{
  uint32_t timeout = 10000;

  rv1103_sfc_putreg(SFC_RESET, SFC_RCVR);
  while ((rv1103_sfc_getreg(SFC_RCVR) & SFC_RESET) != 0)
    {
      if (timeout-- == 0)
        {
          return -ETIMEDOUT;
        }

      up_udelay(1);
    }

  rv1103_sfc_putreg(UINT32_MAX, SFC_ICLR);
  return 0;
}

static int rv1103_sfc_transfer(uint8_t opcode, uint32_t address,
                               unsigned int address_bits,
                               unsigned int dummy_bits, bool write,
                               uint8_t *buffer, size_t size)
{
  uint32_t cmd;
  uint32_t ctrl = SFC_CTRL_SPS | SFC_CTRL_DATALINES_X1;
  uint32_t fsr;
  size_t done = 0;
  int ret;

  fsr = rv1103_sfc_getreg(SFC_FSR);
  if ((fsr & (SFC_TXEMPTY | SFC_RXEMPTY)) !=
      (SFC_TXEMPTY | SFC_RXEMPTY) ||
      (rv1103_sfc_getreg(SFC_SR) & SFC_BUSY) != 0)
    {
      ret = rv1103_sfc_reset();
      if (ret < 0)
        {
          return ret;
        }
    }

  if (size != 0 && buffer == NULL)
    {
      return -EINVAL;
    }

  cmd = opcode | ((uint32_t)dummy_bits << 8) |
        ((uint32_t)(size & 0x3fffu) << 16);
  if (write)
    {
      cmd |= SFC_CMD_RW;
    }

  if (address_bits == 24)
    {
      cmd |= SFC_CMD_ADDR24;
    }
  else if (address_bits != 0)
    {
      cmd |= SFC_CMD_ADDRX;
      ctrl |= SFC_CTRL_ADDRBITS(address_bits);
      rv1103_sfc_putreg(address_bits - 1u, SFC_ABIT);
    }

  rv1103_sfc_putreg(ctrl, SFC_CTRL);
  rv1103_sfc_putreg((uint32_t)size, SFC_LEN_EXT);
  rv1103_sfc_putreg(cmd, SFC_CMD);
  if (address_bits != 0)
    {
      rv1103_sfc_putreg(address, SFC_ADDR);
    }

  while (done < size)
    {
      uint32_t level;
      uint32_t word;
      size_t count;

      fsr = rv1103_sfc_getreg(SFC_FSR);
      level = (fsr >> (write ? 8 : 16)) & 0x1fu;
      if (level == 0)
        {
          static const uint32_t limit = NAND_TRANSFER_TIMEOUT_US;
          uint32_t timeout = limit;

          while (level == 0 && timeout-- != 0)
            {
              up_udelay(1);
              level = (rv1103_sfc_getreg(SFC_FSR) >>
                       (write ? 8 : 16)) & 0x1fu;
            }

          if (level == 0)
            {
              return -ETIMEDOUT;
            }
        }

      while (level-- != 0 && done < size)
        {
          count = size - done;
          if (count > sizeof(word))
            {
              count = sizeof(word);
            }

          if (write)
            {
              word = UINT32_MAX;
              memcpy(&word, buffer + done, count);
              rv1103_sfc_putreg(word, SFC_DATA);
            }
          else
            {
              word = rv1103_sfc_getreg(SFC_DATA);
              memcpy(buffer + done, &word, count);
            }

          done += count;
        }
    }

  ret = rv1103_sfc_wait_idle(NAND_TRANSFER_TIMEOUT_US);
  up_udelay(1);
  return ret;
}

static int rv1103_nand_get_feature(uint8_t address, uint8_t *value)
{
  return rv1103_sfc_transfer(NAND_CMD_GET_FEATURE, address, 8, 0, false,
                             value, 1);
}

static int rv1103_nand_wait_ready(uint8_t *status)
{
  uint32_t timeout = NAND_READY_TIMEOUT_US;
  int ret;

  do
    {
      ret = rv1103_nand_get_feature(NAND_FEATURE_STATUS, status);
      if (ret < 0)
        {
          return ret;
        }

      if ((*status & NAND_STATUS_BUSY) == 0)
        {
          return 0;
        }

      up_udelay(1);
    }
  while (timeout-- != 0);

  return -ETIMEDOUT;
}

static int rv1103_nand_write_enable(void)
{
  uint8_t status;
  int ret;

  ret = rv1103_sfc_transfer(NAND_CMD_WRITE_ENABLE, 0, 0, 0, true,
                             NULL, 0);
  if (ret < 0)
    {
      return ret;
    }

  ret = rv1103_nand_get_feature(NAND_FEATURE_STATUS, &status);
  if (ret < 0)
    {
      return ret;
    }

  return (status & NAND_STATUS_WRITE_ENABLE) != 0 ? 0 : -EACCES;
}

static int rv1103_nand_set_feature(uint8_t address, uint8_t value)
{
  uint8_t status;
  int ret;

  ret = rv1103_nand_write_enable();
  if (ret < 0)
    {
      return ret;
    }

  ret = rv1103_sfc_transfer(NAND_CMD_SET_FEATURE, address, 8, 0, true,
                             &value, 1);
  if (ret < 0)
    {
      return ret;
    }

  return rv1103_nand_wait_ready(&status);
}

static int rv1103_nand_load_page(uint32_t page, uint8_t *ecc)
{
  uint8_t status;
  int ret;

  ret = rv1103_sfc_transfer(NAND_CMD_PAGE_READ, page, 24, 0, true,
                             NULL, 0);
  if (ret < 0)
    {
      return ret;
    }

  ret = rv1103_nand_wait_ready(&status);
  if (ret < 0)
    {
      return ret;
    }

  *ecc = (status >> NAND_STATUS_ECC_SHIFT) & NAND_STATUS_ECC_MASK;
  /* EF AE 21 uses Rockchip ECC status type 0: values 0/1 are healthy,
   * value 2 is corrected but should be refreshed, and value 3 is
   * uncorrectable.
   */

  return *ecc == 3 ? -EBADMSG : 0;
}

static int rv1103_nand_read_cache(uint32_t column, uint8_t *buffer,
                                  size_t size)
{
  return rv1103_sfc_transfer(NAND_CMD_READ_CACHE, column, 16, 8, false,
                             buffer, size);
}

static int rv1103_nand_isbad_locked(uint32_t block)
{
  uint32_t page = block * NAND_PAGES_PER_BLOCK;
  uint8_t marker[2];
  uint8_t ecc;
  int ret;

  ret = rv1103_nand_load_page(page, &ecc);
  if (ret == -EBADMSG)
    {
      return 1;
    }

  if (ret < 0)
    {
      return ret;
    }

  ret = rv1103_nand_read_cache(NAND_PAGE_SIZE, marker, sizeof(marker));
  if (ret < 0)
    {
      return ret;
    }

  return marker[0] != 0xff || marker[1] != 0xff;
}

static bool rv1103_nand_userdata_range(uint32_t offset, uint32_t size)
{
  return offset >= NAND_USERDATA_OFFSET && size <= NAND_USERDATA_SIZE &&
         offset - NAND_USERDATA_OFFSET <= NAND_USERDATA_SIZE - size;
}

static int rv1103_nand_erase_block_locked(uint32_t block)
{
  uint8_t status;
  int ret;

  ret = rv1103_nand_write_enable();
  if (ret < 0)
    {
      return ret;
    }

  ret = rv1103_sfc_transfer(NAND_CMD_BLOCK_ERASE,
                             block * NAND_PAGES_PER_BLOCK,
                             24, 0, true, NULL, 0);
  if (ret < 0)
    {
      return ret;
    }

  ret = rv1103_nand_wait_ready(&status);
  if (ret < 0)
    {
      return ret;
    }

  return (status & NAND_STATUS_ERASE_FAIL) != 0 ? -EIO : 0;
}

static int rv1103_nand_program_page_locked(uint32_t page,
                                            const uint8_t *buffer)
{
  uint8_t status;
  uint8_t ecc;
  int ret;

  memcpy(g_program_buffer, buffer, NAND_PAGE_SIZE);
  memset(g_program_buffer + NAND_PAGE_SIZE, 0xff, NAND_OOB_SIZE);

  ret = rv1103_nand_write_enable();
  if (ret < 0)
    {
      return ret;
    }

  ret = rv1103_sfc_transfer(NAND_CMD_PROGRAM_LOAD, 0, 16, 0, true,
                             g_program_buffer, NAND_PROGRAM_SIZE);
  if (ret < 0)
    {
      return ret;
    }

  ret = rv1103_sfc_transfer(NAND_CMD_PROGRAM_EXECUTE, page, 24, 0, true,
                             NULL, 0);
  if (ret < 0)
    {
      return ret;
    }

  ret = rv1103_nand_wait_ready(&status);
  if (ret < 0)
    {
      return ret;
    }

  if ((status & NAND_STATUS_PROGRAM_FAIL) != 0)
    {
      return -EIO;
    }

  ret = rv1103_nand_load_page(page, &ecc);
  if (ret < 0)
    {
      return ret;
    }

  ret = rv1103_nand_read_cache(0, g_verify_buffer, NAND_PAGE_SIZE);
  if (ret < 0)
    {
      return ret;
    }

  return memcmp(g_verify_buffer, buffer, NAND_PAGE_SIZE) == 0 ? 0 : -EIO;
}

static ssize_t rv1103_nand_read_locked(off_t offset, size_t nbytes,
                                       uint8_t *buffer)
{
  size_t done = 0;

  while (done < nbytes)
    {
      uint32_t page = (uint32_t)offset / NAND_PAGE_SIZE;
      uint32_t column = (uint32_t)offset % NAND_PAGE_SIZE;
      size_t count = NAND_PAGE_SIZE - column;
      uint8_t ecc;
      int ret;

      if (count > nbytes - done)
        {
          count = nbytes - done;
        }

      ret = rv1103_nand_load_page(page, &ecc);
      if (ret < 0)
        {
          return ret;
        }

      ret = rv1103_nand_read_cache(column, buffer + done, count);
      if (ret < 0)
        {
          return ret;
        }

      offset += count;
      done += count;
    }

  return (ssize_t)done;
}

static int rv1103_nand_erase(struct mtd_dev_s *dev, off_t startblock,
                             size_t nblocks)
{
  uint32_t offset;
  uint32_t size;
  size_t i;
  int ret;

  if (startblock < 0 || nblocks > NAND_ERASE_BLOCKS ||
      (size_t)startblock > NAND_ERASE_BLOCKS - nblocks)
    {
      return -EINVAL;
    }

  offset = (uint32_t)startblock * NAND_ERASE_SIZE;
  size = (uint32_t)nblocks * NAND_ERASE_SIZE;
  if (!rv1103_nand_userdata_range(offset, size))
    {
      return -EROFS;
    }

  ret = nxmutex_lock(&g_nand.lock);
  if (ret < 0)
    {
      return ret;
    }

  for (i = 0; i < nblocks; i++)
    {
      uint32_t block = (uint32_t)startblock + i;

      ret = rv1103_nand_isbad_locked(block);
      if (ret != 0)
        {
          ret = ret > 0 ? -EIO : ret;
          break;
        }

      ret = rv1103_nand_erase_block_locked(block);
      if (ret < 0)
        {
          break;
        }
    }

  nxmutex_unlock(&g_nand.lock);
  return ret;
}

static ssize_t rv1103_nand_bread(struct mtd_dev_s *dev, off_t startblock,
                                 size_t nblocks, uint8_t *buffer)
{
  ssize_t ret;
  int lockret;

  if (startblock < 0 || nblocks > NAND_ERASE_BLOCKS * NAND_PAGES_PER_BLOCK ||
      (size_t)startblock > NAND_ERASE_BLOCKS * NAND_PAGES_PER_BLOCK - nblocks)
    {
      return -EINVAL;
    }

  lockret = nxmutex_lock(&g_nand.lock);
  if (lockret < 0)
    {
      return lockret;
    }

  ret = rv1103_nand_read_locked(startblock * NAND_PAGE_SIZE,
                                nblocks * NAND_PAGE_SIZE, buffer);
  nxmutex_unlock(&g_nand.lock);
  return ret < 0 ? ret : (ssize_t)nblocks;
}

static ssize_t rv1103_nand_bwrite(struct mtd_dev_s *dev, off_t startblock,
                                  size_t nblocks, const uint8_t *buffer)
{
  uint32_t offset;
  uint32_t size;
  size_t i;
  int ret;

  if (buffer == NULL || startblock < 0 ||
      nblocks > NAND_ERASE_BLOCKS * NAND_PAGES_PER_BLOCK ||
      (size_t)startblock >
      NAND_ERASE_BLOCKS * NAND_PAGES_PER_BLOCK - nblocks)
    {
      return -EINVAL;
    }

  offset = (uint32_t)startblock * NAND_PAGE_SIZE;
  size = (uint32_t)nblocks * NAND_PAGE_SIZE;
  if (!rv1103_nand_userdata_range(offset, size))
    {
      return -EROFS;
    }

  ret = nxmutex_lock(&g_nand.lock);
  if (ret < 0)
    {
      return ret;
    }

  for (i = 0; i < nblocks; i++)
    {
      ret = rv1103_nand_program_page_locked((uint32_t)startblock + i,
                                             buffer + i * NAND_PAGE_SIZE);
      if (ret < 0)
        {
          break;
        }
    }

  nxmutex_unlock(&g_nand.lock);
  return ret < 0 ? ret : (ssize_t)nblocks;
}

static ssize_t rv1103_nand_read(struct mtd_dev_s *dev, off_t offset,
                                size_t nbytes, uint8_t *buffer)
{
  ssize_t ret;
  int lockret;

  if (offset < 0 || nbytes > NAND_TOTAL_SIZE ||
      (size_t)offset > NAND_TOTAL_SIZE - nbytes)
    {
      return -EINVAL;
    }

  lockret = nxmutex_lock(&g_nand.lock);
  if (lockret < 0)
    {
      return lockret;
    }

  ret = rv1103_nand_read_locked(offset, nbytes, buffer);
  nxmutex_unlock(&g_nand.lock);
  return ret;
}

static int rv1103_nand_ioctl(struct mtd_dev_s *dev, int cmd,
                             unsigned long arg)
{
  switch (cmd)
    {
      case MTDIOC_GEOMETRY:
        {
          struct mtd_geometry_s *geo =
            (struct mtd_geometry_s *)(uintptr_t)arg;

          if (geo == NULL)
            {
              return -EINVAL;
            }

          memset(geo, 0, sizeof(*geo));
          geo->blocksize = NAND_PAGE_SIZE;
          geo->erasesize = NAND_ERASE_SIZE;
          geo->neraseblocks = NAND_ERASE_BLOCKS;
          strlcpy(geo->model, "Winbond EF-AE-21", sizeof(geo->model));
          return 0;
        }

      case MTDIOC_ERASESTATE:
        {
          uint8_t *state = (uint8_t *)(uintptr_t)arg;
          if (state == NULL)
            {
              return -EINVAL;
            }

          *state = 0xff;
          return 0;
        }

      case MTDIOC_BULKERASE:
      case MTDIOC_ERASESECTORS:
      case MTDIOC_PROTECT:
      case MTDIOC_UNPROTECT:
        return -EROFS;

      default:
        return -ENOTTY;
    }
}

static int rv1103_nand_isbad(struct mtd_dev_s *dev, off_t block)
{
  int lockret;
  int ret;

  if (block < 0 || block >= NAND_ERASE_BLOCKS)
    {
      return -EINVAL;
    }

  lockret = nxmutex_lock(&g_nand.lock);
  if (lockret < 0)
    {
      return lockret;
    }

  ret = rv1103_nand_isbad_locked((uint32_t)block);

  nxmutex_unlock(&g_nand.lock);
  return ret;
}

static int rv1103_nand_markbad(struct mtd_dev_s *dev, off_t block)
{
  return -EROFS;
}

static int rv1103_nand_register_partitions(void)
{
  unsigned int i;

  for (i = 0; i < sizeof(g_partitions) / sizeof(g_partitions[0]); i++)
    {
      const struct rv1103_nand_partition_s *desc = &g_partitions[i];
      struct mtd_dev_s *part;
      int ret;

      part = mtd_partition(&g_nand.mtd, desc->offset / NAND_PAGE_SIZE,
                           desc->size / NAND_PAGE_SIZE);
      if (part == NULL)
        {
          return -ENOMEM;
        }

#ifdef CONFIG_MTD_PARTITION_NAMES
      mtd_setpartitionname(part, desc->name);
#endif
      ret = register_mtddriver(desc->path, part,
                               desc->writable ? 0660 : 0444, NULL);
      if (ret < 0)
        {
          return ret;
        }
    }

  return 0;
}

int rv1103_sfc_nand_initialize(void)
{
  uint8_t magic[4];
  int ret;

  if (g_nand.initialized)
    {
      return 0;
    }

  /* A zero write under Rockchip's write-mask convention opens each clock
   * gate and deasserts each reset.  U-Boot has already selected the SFC pins
   * and a valid <=75 MHz source while loading this same image.
   */

  putreg32(RV1103_HCLK_SFC_BIT << 16, RV1103_PERI_CLKGATE_CON4);
  putreg32(RV1103_SCLK_SFC_BIT << 16, RV1103_PERI_CLKGATE_CON5);
  putreg32(RV1103_HCLK_SFC_BIT << 16, RV1103_PERI_SOFTRST_CON4);
  putreg32(RV1103_SCLK_SFC_BIT << 16, RV1103_PERI_SOFTRST_CON5);

  g_nand.version = rv1103_sfc_getreg(SFC_VER) & 0xffffu;
  if (g_nand.version < 4 || g_nand.version > 8)
    {
      return -ENODEV;
    }

  rv1103_sfc_putreg(0, SFC_CTRL);
  rv1103_sfc_putreg(1, SFC_LEN_CTRL);
  rv1103_sfc_putreg(UINT32_MAX, SFC_ICLR);
  rv1103_sfc_putreg(UINT32_MAX, SFC_IMR);

  ret = rv1103_sfc_transfer(NAND_CMD_READ_ID, 0, 8, 0, false,
                             g_nand.id, sizeof(g_nand.id));
  if (ret < 0)
    {
      return ret;
    }

  if (g_nand.id[0] != NAND_ID0 || g_nand.id[1] != NAND_ID1 ||
      g_nand.id[2] != NAND_ID2)
    {
      syslog(LOG_ERR, "ERROR: SFC NAND unsupported id=%02x %02x %02x\n",
             g_nand.id[0], g_nand.id[1], g_nand.id[2]);
      return -ENODEV;
    }

  ret = rv1103_nand_set_feature(NAND_FEATURE_PROTECTION, 0);
  if (ret < 0)
    {
      return ret;
    }

  ret = rv1103_nand_read(&g_nand.mtd, NAND_BOOT_OFFSET,
                          sizeof(magic), magic);
  if (ret != sizeof(magic))
    {
      return ret < 0 ? ret : -EIO;
    }

  ret = register_mtddriver("/dev/spinand0", &g_nand.mtd, 0444, NULL);
  if (ret < 0)
    {
      return ret;
    }

  ret = rv1103_nand_register_partitions();
  if (ret < 0)
    {
      return ret;
    }

  g_nand.initialized = true;
  syslog(LOG_INFO,
         "SFC NAND: v%u id=%02x %02x %02x 128 MiB, 2 KiB page, "
         "128 KiB erase\n",
         g_nand.version, g_nand.id[0], g_nand.id[1], g_nand.id[2]);
  syslog(LOG_INFO, "SFC NAND: boot[0]=%02x%02x%02x%02x; "
                   "userdata writable, all other partitions protected\n",
         magic[0], magic[1], magic[2], magic[3]);
  return 0;
}

#endif
