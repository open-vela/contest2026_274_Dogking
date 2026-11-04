/* SPDX-License-Identifier: Apache-2.0 */
/* Experimental UFSHCI single-slot, polled, read-only host. This initial
 * transport REQUIRES an already powered, initialized, active device/link.
 * It is NOT a cold-start PHY/PMIC driver. No boot probing, no reset, no media
 * automatic writes or formatting. The separate default-off scratch experiment
 * requires explicit acknowledgement for one fixed disposable Linux window.
 * The command parser is deliberately not a raw CDB API.
 */
#include <nuttx/config.h>

#ifdef CONFIG_A733_UFS_READONLY
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/fs/fs.h>
#include <nuttx/fs/ioctl.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include "arm64_internal.h"
#include "a733_ufs_wire.h"
#include "a733_ufs_board.h"
#include "a733_ufs_handoff.h"
#include "a733_ufs_quiesce.h"
#include "a733_ufs_nop.h"
#include "a733_ufs_readcheck.h"
#include "a733_ufs_scratch.h"
#include "a733_ufs_data.h"

#define UFS_BASE UINT64_C(0x04520000)
#define UFS_AXI  UINT64_C(0x02002d80)
#define UFS_CFG  UINT64_C(0x02002d84)
#define UFS_BGR  UINT64_C(0x02002d8c)
#define UFS_FATAL UINT32_C(0x00070884) /* UIC error/link lost + fatal errors */
#define UFS_TIMEOUT_MS 3000

static mutex_t g_ufs_lock = NXMUTEX_INITIALIZER;
static struct
{
  uint8_t *dma;
  bool owns;
  bool registered;
  bool poisoned;
  uint8_t lun;
  uint8_t tag;
  int last;
  uint32_t version;
  uint32_t cap;
  uint32_t hcs;
  uint32_t is;
  uint32_t ocs;
  uint32_t response;
  uint32_t blocksize;
  uint32_t elapsed;
  uint64_t blocks;
  uint64_t commands;
  uint64_t readbytes;
  const char *stage;
  char model[29];
} g_ufs = { .stage = "idle" };
static struct a733_ufs_board g_ufs_board;
static struct a733_ufs_handoff g_ufs_handoff;
static struct a733_ufs_quiesce g_ufs_quiesce;
static struct a733_ufs_nop g_ufs_nop;
static struct a733_ufs_readcheck g_ufs_readcheck;
static bool g_ufs_live_ro;
#ifdef CONFIG_A733_UFS_SCRATCH
static struct a733_ufs_scratch g_ufs_scratch __attribute__((aligned(64)));
#endif
#ifdef CONFIG_A733_UFS_DATA
static struct a733_ufs_data g_ufs_data __attribute__((aligned(64)));
static bool g_ufs_data_registered;
static bool g_ufs_data_ro_registered;
#endif
#ifdef CONFIG_A733_UFS_BLOCK_RO
static uint8_t g_ufs_bench[65536] __attribute__((aligned(64)));
static struct
{
  uint32_t mib, passes, crc[2], elapsed_ms, signature, head_crc;
  uint64_t bytes;
  bool attempted, completed;
  int error;
} g_ufs_benchmark;
#endif
#ifdef CONFIG_A733_UFS_DMA_NOP
static uint8_t g_ufs_nop_arena[A733_UFS_NOP_ARENA]
  __attribute__((aligned(4096)));
#define UFS_NOP_RESERVED 1
#else
#define UFS_NOP_RESERVED 0
#endif
#ifdef CONFIG_A733_UFS_READCHECK
static uint8_t g_ufs_read_arena[A733_UFS_NOP_ARENA]
  __attribute__((aligned(4096)));
#define UFS_READ_RESERVED 1
#else
#define UFS_READ_RESERVED 0
#endif
static char g_ufs_report[10240]; /* Never put a large report on the NSH stack */

static int ufs_board_read(void *context, uint32_t address, uint32_t *value)
{
  (void)context;
  *value = getreg32(address); /* Fixed CCU/RTC allowlist in board sampler */
  return 0;
}

static uint32_t ufs_reg(unsigned int offset)
{
  return getreg32(UFS_BASE + offset);
}

static void ufs_set(unsigned int offset, uint32_t value)
{
  putreg32(value, UFS_BASE + offset);
}

/* No access to the potentially gated HCI window until CCU says all four
 * resets are released and AHB/AXI/config gates enabled. This is a necessary
 * condition, NOT proof that PMIC/power-domain state is safe or media present.
 */
static bool ufs_accessible(void)
{
  return (getreg32(UFS_BGR) & 0xf0001) == 0xf0001 &&
         (getreg32(UFS_AXI) & 0x80000000) &&
         (getreg32(UFS_CFG) & 0x80000000);
}

#ifdef CONFIG_A733_UFS_HANDOFF_PROBE
static int ufs_handoff_read(void *context, uint32_t offset, uint32_t *value)
{
  (void)context;
  /* Recheck always-on gates for EACH allowlisted HCI read. No attempt to
   * enable clocks or recover a missing domain; a hung bus cannot be timed out.
   */
  if (!ufs_accessible())
    {
      return -EAGAIN;
    }

  *value = ufs_reg(offset);
  return 0;
}
#endif

static int ufs_handoff(void)
{
#ifdef CONFIG_A733_UFS_HANDOFF_PROBE
  int ret = a733_ufs_board_sample(&g_ufs_board, ufs_board_read, NULL);
  memset(&g_ufs_handoff, 0, sizeof(g_ufs_handoff));
  if (ret < 0)
    {
      g_ufs_handoff.error = ret;
      g_ufs_handoff.stage = "handoff-board-sample-failed";
    }
  else
    {
      ret = a733_ufs_handoff_snapshot(&g_ufs_handoff, &g_ufs_board,
                                      ufs_handoff_read, NULL);
    }

  g_ufs.stage = g_ufs_handoff.stage;
  return ret;
#else
  g_ufs.stage = "handoff-probe-disabled";
  return -ENOTSUP;
#endif
}

#ifdef CONFIG_A733_UFS_QUIESCE
static int ufs_quiesce_zero(void *context, uint32_t offset)
{
  (void)context;
  if (offset != 0x24 && offset != 0x60 && offset != 0x80) return -EPERM;
  if (!ufs_accessible()) return -EAGAIN;
  UP_DSB();
  ufs_set(offset, 0);
  UP_DSB();
  return 0;
}

static uint32_t ufs_quiesce_now(void *context)
{
  (void)context;
  return (uint32_t)TICK2MSEC(clock_systime_ticks());
}

static void ufs_quiesce_delay(void *context)
{
  (void)context;
  up_udelay(100);
}
#endif

static int ufs_quiesce(void)
{
#ifdef CONFIG_A733_UFS_QUIESCE
  static const struct a733_ufs_quiesce_io io =
  {
    ufs_handoff_read, ufs_quiesce_zero, ufs_quiesce_now, ufs_quiesce_delay
  };
  int ret;
  if (g_ufs.poisoned || g_ufs.owns || g_ufs.registered) return -EBUSY;
  if (g_ufs_quiesce.attempted) return -EALREADY;
  ret = a733_ufs_board_sample(&g_ufs_board, ufs_board_read, NULL);
  if (ret < 0)
    {
      g_ufs.stage = "quiesce-board-sample-failed";
      return ret;
    }

  ret = a733_ufs_quiesce_run(&g_ufs_quiesce, &g_ufs_board, &io, NULL);
  g_ufs.stage = g_ufs_quiesce.stage;
  if (g_ufs_quiesce.quarantine) g_ufs.poisoned = true;
  return ret;
#else
  g_ufs.stage = "quiesce-disabled";
  return -ENOTSUP;
#endif
}

#ifdef CONFIG_A733_UFS_DMA_NOP
static int ufs_nop_write(void *context, uint32_t offset, uint32_t value)
{
  (void)context;
  if (!((offset==0x50 && value==(uint32_t)g_ufs_nop.physical) ||
        (offset==0x54 && value==0) || (offset==0x60 && value<=1) ||
        (offset==0x58 && value==1))) return -EPERM;
  if (!ufs_accessible()) return -EAGAIN;
  UP_DSB();
  ufs_set(offset,value);
  UP_DSB();
  return 0;
}

static void ufs_nop_clean(void *context, uintptr_t address, size_t size)
{
  (void)context;
  up_clean_dcache(address,address+size);
  UP_DSB();
}

static void ufs_nop_invalidate(void *context, uintptr_t address, size_t size)
{
  (void)context;
  UP_DSB();
  up_invalidate_dcache(address,address+size);
  UP_DSB();
}
#endif

static int ufs_dma_nop(void)
{
#ifdef CONFIG_A733_UFS_DMA_NOP
  static const struct a733_ufs_nop_io io =
  {
    ufs_handoff_read, ufs_nop_write, a733_ufs_nop_translate,
    ufs_nop_clean, ufs_nop_invalidate, ufs_quiesce_now, ufs_quiesce_delay
  };
  int ret;
  if (g_ufs.poisoned || g_ufs.owns || g_ufs.registered) return -EBUSY;
  if (g_ufs_nop.attempted) return -EALREADY;
  ret = a733_ufs_board_sample(&g_ufs_board,ufs_board_read,NULL);
  if (ret < 0) {g_ufs.stage="nop-board-sample-failed"; return ret;}
  ret = a733_ufs_nop_run(&g_ufs_nop,&g_ufs_board,&g_ufs_quiesce,
                        g_ufs_nop_arena,&io,NULL);
  g_ufs.stage = g_ufs_nop.stage;
  if (g_ufs_nop.quarantine) g_ufs.poisoned = true;
  return ret;
#else
  g_ufs.stage = "dma-nop-disabled";
  return -ENOTSUP;
#endif
}

#ifdef CONFIG_A733_UFS_READCHECK
static int ufs_readcheck_write(void *context,uint32_t offset,uint32_t value)
{
  (void)context;
  if(!((offset==0x50&&value==(uint32_t)g_ufs_readcheck.physical)||
       (offset==0x54&&value==0)||(offset==0x60&&value<=1)||
       (offset==0x58&&value==1)||(offset==0x20&&value==1)))return -EPERM;
  if(!ufs_accessible())return -EAGAIN;
  UP_DSB();ufs_set(offset,value);UP_DSB();return 0;
}
static const struct a733_ufs_nop_io g_ufs_read_io={ufs_handoff_read,
  ufs_readcheck_write,a733_ufs_nop_translate,ufs_nop_clean,
  ufs_nop_invalidate,ufs_quiesce_now,ufs_quiesce_delay};
#endif

static int ufs_readcheck(uint8_t lun)
{
#ifdef CONFIG_A733_UFS_READCHECK
  if(g_ufs.poisoned||g_ufs.owns||g_ufs.registered)return -EBUSY;
  if(g_ufs_readcheck.attempted)return -EALREADY;
  int ret=a733_ufs_board_sample(&g_ufs_board,ufs_board_read,NULL);
  if(ret<0){g_ufs.stage="readcheck-board-sample-failed";return ret;}
  ret=a733_ufs_readcheck_run(&g_ufs_readcheck,&g_ufs_board,&g_ufs_nop,
                            g_ufs_read_arena,lun,&g_ufs_read_io,NULL);
  g_ufs.stage=g_ufs_readcheck.stage;
  if(g_ufs_readcheck.quarantine)g_ufs.poisoned=true;
  return ret;
#else
  (void)lun;g_ufs.stage="readcheck-disabled";return -ENOTSUP;
#endif
}

static int ufs_snapshot(void)
{
  if (!a733_ufs_board_hci_allowed())
    {
      g_ufs.stage = "board-qualification-required";
      return -EACCES;
    }

  g_ufs.stage = "clock-gate-check";
  if (!ufs_accessible())
    {
      return -EAGAIN;
    }

  g_ufs.cap = ufs_reg(0);
  g_ufs.version = ufs_reg(8);
  g_ufs.hcs = ufs_reg(0x30);
  g_ufs.is = ufs_reg(0x20);
  g_ufs.stage = "register-snapshot-only";
  return 0;
}

static int ufs_poison(int error, const char *stage)
{
  /* Never free or reuse submitted DMA on timeout/error. Do not reset a shared
   * controller or assume clearing a doorbell is a DMA-completion fence.
   * Quarantine until reboot; permanent buffer reservation is reported.
   */
  g_ufs.poisoned = true;
  g_ufs.stage = stage;
  return error;
}

static int ufs_transaction(uint8_t opcode, uint64_t lba, uint32_t count)
{
  clock_t start;
  unsigned int polls;
  int ret;

  if (!a733_ufs_board_hci_allowed() || g_ufs.poisoned ||
      !g_ufs.owns || !ufs_accessible())
    {
      return -EIO;
    }

  if ((ufs_reg(0x34) & 1) == 0 || (ufs_reg(0x30) & 0x33) != 3 ||
      ufs_reg(0x58) || ufs_reg(0x78) || (ufs_reg(0x20) & UFS_FATAL))
    {
      return ufs_poison(-EIO, "pre-submit-state-invalid");
    }

  /* One outstanding transfer in UTRL slot zero, task tag zero. */
  g_ufs.tag = 0;

  ret = ufs_read_command(g_ufs.dma, (uintptr_t)g_ufs.dma, g_ufs.lun,
                         opcode, lba, count, g_ufs.blocksize, g_ufs.tag);
  if (ret < 0)
    {
      return ret;
    }

  /* Dedicated aligned DMA allocation, flat VA=PA only on this A733 port.
   * Clean includes the invalid OCS + zero response/data, preventing dirty CPU
   * cache eviction from overwriting a device-written result later.
   */
  up_clean_dcache((uintptr_t)g_ufs.dma,
                  (uintptr_t)g_ufs.dma + A733_UFS_DMA_SIZE);
  UP_DSB();
  ufs_set(0x20, 1); /* W1C only the completion bit we own */
  ufs_set(0x58, 1);
  ++g_ufs.commands;
  start = clock_systime_ticks();
  /* Two independent bounds even if timer delivery is unexpectedly broken. */
  for (polls = 0; polls < UFS_TIMEOUT_MS * 10; polls++)
    {
      g_ufs.is = ufs_reg(0x20);
      if (g_ufs.is & UFS_FATAL)
        {
          return ufs_poison(-EIO, "transport-fault-quarantined");
        }

      if ((ufs_reg(0x58) & 1) == 0)
        {
          break;
        }

      if (TICK2MSEC(clock_systime_ticks() - start) >= UFS_TIMEOUT_MS)
        {
          break;
        }

      up_udelay(100);
    }

  g_ufs.elapsed = TICK2MSEC(clock_systime_ticks() - start);
  if (ufs_reg(0x58) & 1)
    {
      return ufs_poison(-ETIMEDOUT, "timeout-quarantined-reboot-required");
    }

  UP_DSB();
  up_invalidate_dcache((uintptr_t)g_ufs.dma,
                       (uintptr_t)g_ufs.dma + A733_UFS_DMA_SIZE);
  UP_DSB();
  g_ufs.ocs = ufs_getle32(g_ufs.dma + 8) & 15;
  g_ufs.response = ufs_getbe32(g_ufs.dma + A733_UFS_RSP_OFFSET + 4);
  ret = ufs_read_response(g_ufs.dma, g_ufs.lun, opcode, g_ufs.tag);
  if (ret < 0)
    {
      return ufs_poison(ret, "response-rejected-quarantined");
    }

  return 0;
}

static int ufs_block_geometry(struct inode *inode, struct geometry *geo)
{
  int ret = nxmutex_lock(&g_ufs_lock);
  (void)inode;
  if (ret < 0)
    {
      return ret;
    }

  memset(geo, 0, sizeof(*geo));
  geo->geo_available = g_ufs.registered && !g_ufs.poisoned;
  geo->geo_writeenabled = false;
  geo->geo_nsectors = g_ufs.blocks;
  geo->geo_sectorsize = g_ufs.blocksize;
  strlcpy(geo->geo_model, g_ufs.model, sizeof(geo->geo_model));
  nxmutex_unlock(&g_ufs_lock);
  return 0;
}

static ssize_t ufs_block_read(struct inode *inode, unsigned char *buffer,
                              blkcnt_t start, unsigned int count)
{
  uint64_t lba = (uint64_t)start;
  unsigned int done = 0;
  int ret = nxmutex_lock(&g_ufs_lock);
  (void)inode;
  if (ret < 0)
    {
      return ret;
    }

  if(g_ufs_live_ro)
    {
#ifdef CONFIG_A733_UFS_BLOCK_RO
      if(!g_ufs.registered||g_ufs.poisoned)ret=-ENODEV;
      else if(start<0)ret=-EINVAL;
      else ret=a733_ufs_reader_read(&g_ufs_readcheck,&g_ufs_board,
        g_ufs_read_arena,buffer,(uint64_t)start,count,&g_ufs_read_io,NULL);
      if(g_ufs_readcheck.quarantine)g_ufs.poisoned=true;
      g_ufs.last=ret;g_ufs.stage=g_ufs_readcheck.stage;
      nxmutex_unlock(&g_ufs_lock);
      return ret<0?ret:(ssize_t)count;
#else
      nxmutex_unlock(&g_ufs_lock);return -ENOTSUP;
#endif
    }

  if (!g_ufs.registered || g_ufs.poisoned)
    {
      ret = -ENODEV;
    }
  else
    {
      ret = ufs_read_range(lba, count, g_ufs.blocks);
    }

  while (ret == 0 && done < count)
    {
      unsigned int chunk = A733_UFS_TRANSFER_MAX / g_ufs.blocksize;
      if (chunk > count - done)
        {
          chunk = count - done;
        }

      ret = ufs_transaction(0x88, lba + done, chunk);
      if (ret == 0)
        {
          size_t bytes = (size_t)chunk * g_ufs.blocksize;
          memcpy(buffer + (size_t)done * g_ufs.blocksize,
                 g_ufs.dma + A733_UFS_DATA_OFFSET, bytes);
          g_ufs.readbytes += bytes;
          done += chunk;
        }
    }

  g_ufs.last = ret;
  if (ret == 0)
    {
      g_ufs.stage = "readonly-block-read-completed";
    }

  nxmutex_unlock(&g_ufs_lock);
  return ret < 0 ? ret : (ssize_t)done;
}

static const struct block_operations g_ufs_block_ops =
{
  .read = ufs_block_read,
  .geometry = ufs_block_geometry,
  /* No write, ioctl/pass-through, discard, format, or automatic partitioning. */
};

static int ufs_publish_ro(void)
{
#ifdef CONFIG_A733_UFS_BLOCK_RO
  if(g_ufs.poisoned)return -EIO;
  if(g_ufs.registered)return -EALREADY;
  int ret=a733_ufs_reader_ready(&g_ufs_readcheck,&g_ufs_board,
    g_ufs_read_arena,&g_ufs_read_io,NULL);
  if(g_ufs_readcheck.quarantine)g_ufs.poisoned=true;
  if(ret)return ret;
  if((uint64_t)(blkcnt_t)g_ufs_readcheck.blocks!=g_ufs_readcheck.blocks||
     (blkcnt_t)g_ufs_readcheck.blocks<=0)return -EOVERFLOW;
  ret=register_blockdriver("/dev/ufs0",&g_ufs_block_ops,0444,NULL);
  if(ret){g_ufs.stage="block-ro-registration-failed";return ret;}
  g_ufs_live_ro=true;g_ufs.registered=true;
  g_ufs.lun=g_ufs_readcheck.lun;g_ufs.blocks=g_ufs_readcheck.blocks;
  g_ufs.blocksize=g_ufs_readcheck.blocksize;
  memcpy(g_ufs.model,g_ufs_readcheck.model,sizeof(g_ufs.model));
  g_ufs.stage="block-ro-registered-not-mounted";
  return 0;
#else
  return -ENOTSUP;
#endif
}

static int ufs_bench_ro(unsigned mib)
{
#ifdef CONFIG_A733_UFS_BLOCK_RO
  if(mib<1||mib>16)return -EINVAL;
  if(!g_ufs_live_ro||!g_ufs.registered||g_ufs.poisoned)return -ENODEV;
  if(g_ufs_benchmark.attempted)return -EALREADY;
  g_ufs_benchmark.attempted=true;g_ufs_benchmark.mib=mib;
  uint64_t bytes=(uint64_t)mib*1048576;
  if(bytes/g_ufs.blocksize>g_ufs.blocks)
    {g_ufs_benchmark.error=-EOVERFLOW;return -EOVERFLOW;}
  uint32_t start=ufs_quiesce_now(NULL);
  int ret=0;
  for(unsigned pass=0;pass<2&&!ret;pass++)
    {
      uint32_t crc=UINT32_MAX;
      for(uint64_t offset=0;offset<bytes;offset+=sizeof(g_ufs_bench))
        {
          ret=a733_ufs_reader_read(&g_ufs_readcheck,&g_ufs_board,
            g_ufs_read_arena,g_ufs_bench,offset/g_ufs.blocksize,
            sizeof(g_ufs_bench)/g_ufs.blocksize,&g_ufs_read_io,NULL);
          if(ret)break;
          if(!pass&&!offset)
            g_ufs_benchmark.signature=ufs_getle32(g_ufs_bench+510)&0xffff;
          for(size_t i=0;i<sizeof(g_ufs_bench);i++)
            {
              crc^=g_ufs_bench[i];
              for(unsigned j=0;j<8;j++)
                crc=(crc>>1)^((0-(crc&1))&UINT32_C(0xedb88320));
              if(!pass&&!offset&&i==A733_UFS_READ_SAMPLE-1)
                {
                  g_ufs_benchmark.head_crc=~crc;
                  if(g_ufs_benchmark.head_crc!=g_ufs_readcheck.crc[0])
                    {ret=-EBADMSG;g_ufs.poisoned=true;break;}
                }
            }
          if(ret)break;
          g_ufs_benchmark.bytes+=sizeof(g_ufs_bench);
          if(ufs_quiesce_now(NULL)-start>30000)
            {ret=-ETIMEDOUT;break;} /* Completed/idle: no reset or retry. */
        }
      g_ufs_benchmark.crc[pass]=~crc;
      if(!ret)++g_ufs_benchmark.passes;
    }
  if(!ret&&g_ufs_benchmark.crc[0]!=g_ufs_benchmark.crc[1])
    {ret=-EBADMSG;g_ufs.poisoned=true;}
  if(g_ufs_readcheck.quarantine)g_ufs.poisoned=true;
  g_ufs_benchmark.elapsed_ms=ufs_quiesce_now(NULL)-start;
  g_ufs_benchmark.error=ret;g_ufs_benchmark.completed=!ret;
  g_ufs.stage=ret?"bench-ro-failed":"bench-ro-repeat-passed-not-mounted";
  return ret;
#else
  (void)mib;return -ENOTSUP;
#endif
}

static int ufs_attach(uint8_t lun)
{
  uintptr_t physical;
  uint8_t *data;
  unsigned int i;
  int ret;

  if (g_ufs.poisoned)
    {
      return -EIO;
    }

  if (g_ufs.registered)
    {
      return -EBUSY; /* Never switch a mounted/open device's LUN */
    }

  ret = ufs_snapshot();
  if (ret < 0)
    {
      return ret;
    }

  g_ufs.stage = "inherited-active-link-required";
  if (g_ufs.version < 0x200 || g_ufs.version > 0x310 ||
      (ufs_reg(0x34) & 1) == 0 || (g_ufs.hcs & 0x33) != 3)
    {
      return -ENOLINK;
    }

  /* Do not steal outstanding firmware requests or a running task engine.
   * This is explicit manual ownership transfer, never a transparent attach.
   * Automatic hibernate is unsupported, and interrupts must be disabled.
   */
  g_ufs.stage = "inherited-engine-must-be-idle";
  if (ufs_reg(0x24) || ufs_reg(0x18) || ufs_reg(0x58) || ufs_reg(0x78) ||
      ufs_reg(0x80) || (!g_ufs.owns && ufs_reg(0x60)) ||
      (g_ufs.is & UFS_FATAL))
    {
      return -EBUSY;
    }

  if (!g_ufs.dma)
    {
      g_ufs.dma = kmm_memalign(1024, A733_UFS_DMA_SIZE);
      if (!g_ufs.dma)
        {
          return -ENOMEM;
        }
    }

  physical = (uintptr_t)g_ufs.dma;
  if ((physical & 1023) || physical > UINTPTR_MAX - A733_UFS_DMA_SIZE ||
      (uint64_t)physical + A733_UFS_DMA_SIZE > UINT64_C(0x100000000))
    {
      /* HCI CAP64 alone does not prove the SoC/interconnect DMA aperture.
       * Keep this first port low32 even if the IP advertises 64-bit support.
       * No base published, no DMA submitted yet; reject rather than truncate.
       */
      kmm_free(g_ufs.dma);
      g_ufs.dma = NULL;
      return -EADDRNOTAVAIL;
    }

  memset(g_ufs.dma, 0, A733_UFS_DMA_SIZE);
  up_clean_dcache(physical, physical + A733_UFS_DMA_SIZE);
  UP_DSB();
  ufs_set(0x50, (uint32_t)physical);
  ufs_set(0x54, (uint32_t)((uint64_t)physical >> 32));
  ufs_set(0x60, 1);
  g_ufs.owns = true;
  g_ufs.lun = lun;
  g_ufs.stage = "nop";
  ret = ufs_transaction(0, 0, 0);
  if (ret < 0) return ret;
  g_ufs.stage = "inquiry";
  ret = ufs_transaction(0x12, 0, 0);
  if (ret < 0) return ret;
  data = g_ufs.dma + A733_UFS_DATA_OFFSET;
  if ((data[0] & 0xe0) || (data[0] & 31) != 0 || data[4] < 31)
    {
      return ufs_poison(-ENODEV, "not-a-direct-access-lun");
    }

  /* Only printable vendor/product/revision, no serial number or secrets. */
  for (i = 0; i < 28; i++)
    {
      g_ufs.model[i] = data[8 + i] >= 32 && data[8 + i] <= 126 ?
                       data[8 + i] : '?';
    }

  g_ufs.model[28] = 0;
  g_ufs.stage = "read-capacity-16";
  ret = ufs_transaction(0x9e, 0, 0);
  if (ret < 0) return ret;
  ret = ufs_read_capacity(g_ufs.dma + A733_UFS_DATA_OFFSET,
                          &g_ufs.blocks, &g_ufs.blocksize);
  if (ret < 0 || (uint64_t)(blkcnt_t)g_ufs.blocks != g_ufs.blocks ||
      (blkcnt_t)g_ufs.blocks <= 0)
    {
      return ufs_poison(-EOVERFLOW, "unsupported-capacity");
    }

  ret = register_blockdriver("/dev/ufs0", &g_ufs_block_ops, 0444, NULL);
  g_ufs.registered = ret == 0;
  g_ufs.stage = ret == 0 ? "readonly-block-registered" : "block-register-failed";
  return ret;
}

#ifdef CONFIG_A733_UFS_SCRATCH
static int ufs_scratch(bool test)
{
  if(g_ufs.poisoned)return -EIO;
  if(!g_ufs_live_ro||!g_ufs.registered||!g_ufs_benchmark.completed||
     g_ufs_benchmark.error||g_ufs_benchmark.passes!=2||
     g_ufs_benchmark.bytes<UINT64_C(16777216)||
     g_ufs_benchmark.crc[0]!=g_ufs_benchmark.crc[1]||
     g_ufs_benchmark.head_crc!=g_ufs_readcheck.crc[0])return -EACCES;
  int ret=test?a733_ufs_scratch_test(&g_ufs_scratch,&g_ufs_readcheck,
    &g_ufs_board,g_ufs_read_arena,g_ufs_bench,&g_ufs_read_io,NULL):
    a733_ufs_scratch_prepare(&g_ufs_scratch,&g_ufs_readcheck,
    &g_ufs_board,g_ufs_read_arena,g_ufs_bench,&g_ufs_read_io,NULL);
  if(g_ufs_scratch.quarantine||g_ufs_readcheck.quarantine)g_ufs.poisoned=true;
  g_ufs.stage=g_ufs_scratch.stage;return ret;
}
static ssize_t ufs_backup_read(struct file *filep,char *buffer,size_t size)
{
  int ret=nxmutex_lock(&g_ufs_lock);if(ret)return ret;
  ssize_t amount=-ENODATA;
  if(g_ufs_scratch.prepared)
    {
      amount=filep->f_pos<0||(uint64_t)filep->f_pos>=A733_UFS_BACKUP_BYTES?0:
        A733_UFS_BACKUP_BYTES-(size_t)filep->f_pos;
      if((size_t)amount>size)amount=size;
      if(amount)memcpy(buffer,g_ufs_scratch.original+filep->f_pos,(size_t)amount);
      filep->f_pos+=amount;
    }
  nxmutex_unlock(&g_ufs_lock);return amount;
}
static const struct file_operations g_ufs_backup_ops={.read=ufs_backup_read};
#endif

#ifdef CONFIG_A733_UFS_DATA
static int ufs_data_ro_geometry(struct inode *inode,struct geometry *geo)
{
  (void)inode;int ret=nxmutex_lock(&g_ufs_lock);if(ret)return ret;
  memset(geo,0,sizeof(*geo));
  geo->geo_available=g_ufs_data_ro_registered&&!g_ufs.poisoned;
  geo->geo_writeenabled=false;geo->geo_nsectors=A733_UFS_DATA_BLOCKS;
  geo->geo_sectorsize=4096;strlcpy(geo->geo_model,g_ufs.model,sizeof(geo->geo_model));
  nxmutex_unlock(&g_ufs_lock);return 0;
}
static ssize_t ufs_data_ro_read(struct inode *inode,unsigned char *buf,
                                blkcnt_t start,unsigned int count)
{
  (void)inode;int ret=nxmutex_lock(&g_ufs_lock);if(ret)return ret;
  if(!g_ufs_data_ro_registered||g_ufs.poisoned)ret=-ENODEV;
  else if(start<0)ret=-EINVAL;
  else ret=a733_ufs_data_ro_read(&g_ufs_readcheck,&g_ufs_board,
    g_ufs_read_arena,buf,(uint64_t)start,count,&g_ufs_read_io,NULL);
  if(g_ufs_readcheck.quarantine)g_ufs.poisoned=true;
  g_ufs.last=ret;g_ufs.stage=g_ufs_readcheck.stage;
  nxmutex_unlock(&g_ufs_lock);return ret?ret:(ssize_t)count;
}
static const struct block_operations g_ufs_data_ro_ops=
{.read=ufs_data_ro_read,.geometry=ufs_data_ro_geometry};
static int ufs_publish_data_ro(void)
{
  if(g_ufs.poisoned||!g_ufs_live_ro||!g_ufs.registered)return -EACCES;
  if(g_ufs_data_ro_registered)return -EALREADY;
  if(g_ufs_data_registered||g_ufs_data.enabled)return -EBUSY;
  int ret=a733_ufs_data_ro_ready(&g_ufs_readcheck,&g_ufs_board,
    g_ufs_read_arena,&g_ufs_read_io,NULL);
  if(!ret)ret=register_blockdriver("/dev/ufsdata-ro",&g_ufs_data_ro_ops,0444,NULL);
  if(!ret){g_ufs_data_ro_registered=true;g_ufs.stage="data-ro-registered-NO-media-writes";}
  if(g_ufs_readcheck.quarantine)g_ufs.poisoned=true;
  return ret;
}
static int ufs_data_geometry(struct inode *inode,struct geometry *geo)
{
  (void)inode;int ret=nxmutex_lock(&g_ufs_lock);if(ret)return ret;
  memset(geo,0,sizeof(*geo));
  geo->geo_available=g_ufs_data_registered&&!g_ufs.poisoned&&!g_ufs_data.quarantine;
  geo->geo_writeenabled=geo->geo_available;
  geo->geo_nsectors=A733_UFS_DATA_BLOCKS;geo->geo_sectorsize=4096;
  strlcpy(geo->geo_model,g_ufs.model,sizeof(geo->geo_model));
  nxmutex_unlock(&g_ufs_lock);return 0;
}
static ssize_t ufs_data_transfer(unsigned char *buf,blkcnt_t start,
                                 unsigned int count,bool writing)
{
  int ret=nxmutex_lock(&g_ufs_lock);if(ret)return ret;
  if(!g_ufs_data_registered||g_ufs.poisoned)ret=-ENODEV;
  else if(start<0)ret=-EINVAL;
  else ret=a733_ufs_data_io(&g_ufs_data,&g_ufs_scratch,&g_ufs_readcheck,
    &g_ufs_board,g_ufs_read_arena,g_ufs_bench,buf,(uint64_t)start,count,
    writing,&g_ufs_read_io,NULL);
  if(g_ufs_data.quarantine||g_ufs_readcheck.quarantine)g_ufs.poisoned=true;
  g_ufs.last=ret;g_ufs.stage=g_ufs_data.stage;
  nxmutex_unlock(&g_ufs_lock);return ret?ret:(ssize_t)count;
}
static ssize_t ufs_data_read(struct inode *inode,unsigned char *buf,
                              blkcnt_t start,unsigned int count)
{(void)inode;return ufs_data_transfer(buf,start,count,false);}
static ssize_t ufs_data_write(struct inode *inode,const unsigned char *buf,
                               blkcnt_t start,unsigned int count)
{(void)inode;return ufs_data_transfer((unsigned char *)buf,start,count,true);}
static int ufs_data_ioctl(struct inode *inode,int cmd,unsigned long arg)
{
  (void)inode;
  if(cmd!=BIOC_FLUSH||arg)return -ENOTTY; /* NO passthrough/discard/format. */
  int ret=nxmutex_lock(&g_ufs_lock);if(ret)return ret;
  if(!g_ufs_data_registered||g_ufs.poisoned)ret=-ENODEV;
  else ret=a733_ufs_data_flush(&g_ufs_data,&g_ufs_scratch,&g_ufs_readcheck,
    &g_ufs_board,g_ufs_read_arena,g_ufs_bench,&g_ufs_read_io,NULL);
  if(g_ufs_data.quarantine||g_ufs_readcheck.quarantine)g_ufs.poisoned=true;
  g_ufs.last=ret;g_ufs.stage=g_ufs_data.stage;
  nxmutex_unlock(&g_ufs_lock);return ret;
}
static const struct block_operations g_ufs_data_ops=
{
  .read=ufs_data_read,.write=ufs_data_write,
  .geometry=ufs_data_geometry,.ioctl=ufs_data_ioctl,
};
static int ufs_publish_data_mode(bool existing)
{
  if(g_ufs.poisoned||!g_ufs_live_ro||!g_ufs.registered)return -EACCES;
  if(g_ufs_data_ro_registered)return -EBUSY; /* One mode per boot: no alias mounts. */
  if(g_ufs_data_registered)return -EALREADY;
  int ret=existing?
    a733_ufs_data_existing(&g_ufs_data,&g_ufs_scratch,&g_ufs_readcheck,
      &g_ufs_board,g_ufs_read_arena,g_ufs_bench,&g_ufs_read_io,NULL):
    a733_ufs_data_enable(&g_ufs_data,&g_ufs_scratch,&g_ufs_readcheck,
      &g_ufs_board,g_ufs_read_arena,g_ufs_bench,&g_ufs_read_io,NULL);
  if(!ret)ret=register_blockdriver("/dev/ufsdata",&g_ufs_data_ops,0600,NULL);
  if(!ret)g_ufs_data_registered=true;
  if(g_ufs_data.quarantine||g_ufs_readcheck.quarantine)g_ufs.poisoned=true;
  g_ufs.stage=g_ufs_data.stage;return ret;
}
static int ufs_publish_data(void){return ufs_publish_data_mode(false);}
#endif

static ssize_t ufs_diag_read(struct file *filep, char *buffer, size_t buflen)
{
  size_t length;
  size_t copy;
  int ret = nxmutex_lock(&g_ufs_lock);
  if (ret < 0) return ret;
  length = snprintf(g_ufs_report, sizeof(g_ufs_report),
    "A733 UFS v175 automount candidate (/dev/ufs0 STILL readonly)\n"
    "last=%d stage=%s registered=%u quarantine=%u owns=%u lun=%u\n"
    "ccu: axi=%08" PRIx32 " cfg=%08" PRIx32 " bgr=%08" PRIx32 "\n"
    "last-snapshot: version=%08" PRIx32 " cap=%08" PRIx32
    " hcs=%08" PRIx32 " is=%08" PRIx32 " ocs=%" PRIu32
    " response=%08" PRIx32 " elapsed-ms=%" PRIu32 "\n"
    "media: model='%s' blocks=%" PRIu64 " block-bytes=%" PRIu32
    " capacity-bytes=%" PRIu64 " commands=%" PRIu64 " read-bytes=%" PRIu64 "\n"
    "dma: resident=%u bytes=%u retained-until-reboot=1\n"
    "control: board | handoff | quiesce | dma-nop | readcheck[=<lun:0..7>] | block-ro | bench-ro[=<MiB:1..16>] | probe | attach-ro=<lun:0..7>\n"
    "board: read-only CCU/RTC collection; no HCI/PMIC/DMA/clock writes\n"
    "probe/attach-ro: blocked until real board power provider is qualified\n"
    "handoff: explicit experimental firmware-powered HCI snapshot; no writes\n"
    "quiesce: manual zero-only IE/runstops; no DMA/media/reset; once per boot\n"
    "nop-arena: reserved=%u bytes=%u; isolated static BSS, never freed\n"
    "read-arena: reserved=%u bytes=%u; separate static BSS, never freed\n"
    "probe snapshots registers only; does NOT prove device/link presence\n"
    "attach-ro REQUIRES firmware-initialized active idle link and device\n"
    "cold-PMIC/PHY-init=not-implemented full-device-writes=disabled auto-mount=optional-ufs-service\n"
    "SD boot and /data unchanged; no reset, timeout retry, format, migration or erase\n",
    g_ufs.last, g_ufs.stage, g_ufs.registered, g_ufs.poisoned,
    g_ufs.owns, g_ufs.lun, getreg32(UFS_AXI), getreg32(UFS_CFG),
    getreg32(UFS_BGR), g_ufs.version, g_ufs.cap, g_ufs.hcs, g_ufs.is,
    g_ufs.ocs, g_ufs.response, g_ufs.elapsed, g_ufs.model, g_ufs.blocks,
    g_ufs.blocksize, g_ufs.blocks * g_ufs.blocksize, g_ufs.commands,
    g_ufs.readbytes, g_ufs.dma != NULL, A733_UFS_DMA_SIZE,
    UFS_NOP_RESERVED,A733_UFS_NOP_ARENA,UFS_READ_RESERVED,A733_UFS_NOP_ARENA);
  if (length >= sizeof(g_ufs_report)) length = sizeof(g_ufs_report) - 1;
  length += a733_ufs_board_report(&g_ufs_board, g_ufs_report + length,
                                  sizeof(g_ufs_report) - length);
  length += a733_ufs_handoff_report(&g_ufs_handoff, g_ufs_report + length,
                                    sizeof(g_ufs_report) - length);
  length += a733_ufs_quiesce_report(&g_ufs_quiesce, g_ufs_report + length,
                                    sizeof(g_ufs_report) - length);
  length += a733_ufs_nop_report(&g_ufs_nop,g_ufs_report+length,
                                sizeof(g_ufs_report)-length);
  length += a733_ufs_readcheck_report(&g_ufs_readcheck,g_ufs_report+length,
                                      sizeof(g_ufs_report)-length);
  int extra=snprintf(g_ufs_report+length,sizeof(g_ufs_report)-length,
    "block-v171: published=%u node=/dev/ufs0 readonly=1 chunk-max=65536 request-max=1048576; no media writes/FS/mount/partition auto-registration\n",
    g_ufs_live_ro);
  if(extra>0)length+=(size_t)extra<sizeof(g_ufs_report)-length?(size_t)extra:sizeof(g_ufs_report)-length-1;
#ifdef CONFIG_A733_UFS_BLOCK_RO
  extra=snprintf(g_ufs_report+length,sizeof(g_ufs_report)-length,
    "bench-ro-v171: attempted=%u completed=%u error=%d MiB-per-pass=%" PRIu32 " passes=%" PRIu32
    " bytes=%" PRIu64 " elapsed-ms=%" PRIu32 " crc=%08" PRIx32 "/%08" PRIx32 " mbr=%04" PRIx32 " head16-crc=%08" PRIx32 "\n",
    g_ufs_benchmark.attempted,g_ufs_benchmark.completed,g_ufs_benchmark.error,
    g_ufs_benchmark.mib,g_ufs_benchmark.passes,g_ufs_benchmark.bytes,
    g_ufs_benchmark.elapsed_ms,g_ufs_benchmark.crc[0],g_ufs_benchmark.crc[1],g_ufs_benchmark.signature,g_ufs_benchmark.head_crc);
  if(extra>0)length+=(size_t)extra<sizeof(g_ufs_report)-length?(size_t)extra:sizeof(g_ufs_report)-length-1;
#endif
#ifdef CONFIG_A733_UFS_SCRATCH
  length+=a733_ufs_scratch_report(&g_ufs_scratch,g_ufs_report+length,
                                 sizeof(g_ufs_report)-length);
#endif
#ifdef CONFIG_A733_UFS_DATA
  length+=a733_ufs_data_report(&g_ufs_data,g_ufs_report+length,
                              sizeof(g_ufs_report)-length);
  extra=snprintf(g_ufs_report+length,sizeof(g_ufs_report)-length,
    "recovery-v174: published=%u node=/dev/ufsdata-ro readonly=1; manual data-ro after block-ro, no scratch/flush/media-write; RO/RW mutually exclusive per boot\n",
    g_ufs_data_ro_registered);
  if(extra>0)length+=(size_t)extra<sizeof(g_ufs_report)-length?(size_t)extra:sizeof(g_ufs_report)-length-1;
#endif
  copy = filep->f_pos < 0 || (uint64_t)filep->f_pos >= length ? 0 :
           length - (size_t)filep->f_pos;
  if (copy > buflen) copy = buflen;
  if (copy) memcpy(buffer, g_ufs_report + filep->f_pos, copy);
  filep->f_pos += copy;
  nxmutex_unlock(&g_ufs_lock);
  return copy;
}

static ssize_t ufs_diag_write(struct file *filep, const char *buffer,
                               size_t buflen)
{
  char command[32];
  size_t length = buflen;
  int ret;
  (void)filep;
  if (!length) return 0;
  if (length >= sizeof(command) || memchr(buffer, 0, length))
    {
      return -EINVAL;
    }

  memcpy(command, buffer, length);
  while (length && (command[length - 1] == '\n' ||
                    command[length - 1] == '\r')) --length;
  command[length] = 0;
  /* NSH echo writes the argument and its newline separately. Consume an
   * empty CR/LF write without overwriting the actual command result/stage.
   */
  if (!length) return (ssize_t)buflen;
  ret = nxmutex_trylock(&g_ufs_lock);
  if (ret < 0) return ret;
  if (strcmp(command, "board") == 0)
    {
      ret = a733_ufs_board_sample(&g_ufs_board, ufs_board_read, NULL);
      g_ufs.stage = g_ufs_board.stage;
    }
  else if (strcmp(command, "handoff") == 0)
    {
      ret = ufs_handoff();
    }
  else if (strcmp(command, "quiesce") == 0)
    {
      ret = ufs_quiesce();
    }
  else if (strcmp(command, "dma-nop") == 0)
    {
      ret = ufs_dma_nop();
    }
  else if (strcmp(command, "probe") == 0)
    {
      ret = g_ufs.poisoned ? -EIO : ufs_snapshot();
    }
  else if(strcmp(command,"block-ro")==0)ret=ufs_publish_ro();
  else if(strcmp(command,"bench-ro")==0)ret=ufs_bench_ro(8);
#ifdef CONFIG_A733_UFS_SCRATCH
  else if(strcmp(command,"scratch-prepare")==0)ret=ufs_scratch(false);
  else if(strcmp(command,"scratch-test=ERASE-TEST-LINUX")==0)ret=ufs_scratch(true);
#endif
#ifdef CONFIG_A733_UFS_DATA
  else if(strcmp(command,"data-ro")==0)ret=ufs_publish_data_ro();
  else if(strcmp(command,"data-existing")==0)ret=ufs_publish_data_mode(true);
  else if(strcmp(command,"data-rw=ERASE-TEST-LINUX")==0)ret=ufs_publish_data();
#endif
  else if(!strncmp(command,"bench-ro=",9))
    {
      unsigned mib=0;size_t n=length-9;
      if(!n||n>2)ret=-EINVAL;
      else
        {
          ret=0;
          for(size_t i=9;i<length;i++)
            if(command[i]<'0'||command[i]>'9'){ret=-EINVAL;break;}
            else mib=mib*10+(unsigned)(command[i]-'0');
          if(!ret)ret=ufs_bench_ro(mib);
        }
    }
  else if(strcmp(command,"readcheck")==0)
    {
      ret=ufs_readcheck(0);
    }
  else if(length==11&&memcmp(command,"readcheck=",10)==0&&
          command[10]>='0'&&command[10]<='7')
    {
      ret=ufs_readcheck((uint8_t)(command[10]-'0'));
    }
  else if (length == 11 && memcmp(command, "attach-ro=", 10) == 0 &&
           command[10] >= '0' && command[10] <= '7')
    {
      ret = ufs_attach(command[10] - '0');
    }
  else
    {
      ret = -EINVAL;
    }

  g_ufs.last = ret;
  nxmutex_unlock(&g_ufs_lock);
  return ret < 0 ? ret : (ssize_t)buflen;
}

static const struct file_operations g_ufs_diag_ops =
{
  .read = ufs_diag_read,
  .write = ufs_diag_write,
};

int a733_ufs_initialize(void)
{
  /* Registration ONLY. Do not touch HCI, clocks, reset or storage at boot. */
#ifdef CONFIG_A733_UFS_SCRATCH
  int ret=register_driver("/dev/a733-ufs-backup",&g_ufs_backup_ops,0444,NULL);
  if(ret)return ret;
#endif
  return register_driver("/dev/a733-ufs", &g_ufs_diag_ops, 0600, NULL);
}
#endif
