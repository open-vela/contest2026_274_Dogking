/****************************************************************************
 * vendor/rockchip/chips/rv1103/rv1103_dma.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_RV1103_PL330_DMA

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/cache.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>

#include <arch/chip/rv1103_dma.h>

#include "arm_internal.h"

/* The base address and CRU controls are from the official RV1106 DTS and
 * CMSIS header.  RV1103 uses the same PL330 integration:
 *
 *   dmac@ff420000
 *   PERICRU_GATE_CON05[8]     ACLK_DMAC
 *   PERICRU_SOFTRST_CON05[8] ARESETN_DMAC
 *
 * This first stage deliberately uses synchronous polling.  No GIC interrupt
 * is enabled until channel-event routing has been independently verified.
 */

#define RV1103_DMAC_BASE                 0xff420000u
#define RV1103_DMAC_DS                   (RV1103_DMAC_BASE + 0x000u)
#define RV1103_DMAC_DPC                  (RV1103_DMAC_BASE + 0x004u)
#define RV1103_DMAC_INTEN                (RV1103_DMAC_BASE + 0x020u)
#define RV1103_DMAC_INTCLR               (RV1103_DMAC_BASE + 0x02cu)
#define RV1103_DMAC_FSM                  (RV1103_DMAC_BASE + 0x030u)
#define RV1103_DMAC_FSC                  (RV1103_DMAC_BASE + 0x034u)
#define RV1103_DMAC_FTM                  (RV1103_DMAC_BASE + 0x038u)
#define RV1103_DMAC_FTC(n)               (RV1103_DMAC_BASE + 0x040u + \
                                          (n) * 4u)
#define RV1103_DMAC_CS(n)                (RV1103_DMAC_BASE + 0x100u + \
                                          (n) * 8u)
#define RV1103_DMAC_CPC(n)               (RV1103_DMAC_BASE + 0x104u + \
                                          (n) * 8u)
#define RV1103_DMAC_SA(n)                (RV1103_DMAC_BASE + 0x400u + \
                                          (n) * 0x20u)
#define RV1103_DMAC_DA(n)                (RV1103_DMAC_BASE + 0x404u + \
                                          (n) * 0x20u)
#define RV1103_DMAC_DBGSTATUS            (RV1103_DMAC_BASE + 0xd00u)
#define RV1103_DMAC_DBGCMD               (RV1103_DMAC_BASE + 0xd04u)
#define RV1103_DMAC_DBGINST0             (RV1103_DMAC_BASE + 0xd08u)
#define RV1103_DMAC_DBGINST1             (RV1103_DMAC_BASE + 0xd0cu)
#define RV1103_DMAC_CR0                  (RV1103_DMAC_BASE + 0xe00u)
#define RV1103_DMAC_CRD                  (RV1103_DMAC_BASE + 0xe14u)
#define RV1103_DMAC_PID(n)               (RV1103_DMAC_BASE + 0xfe0u + \
                                          (n) * 4u)

#define RV1103_PERI_CLKGATE_CON5         0xff3b2814u
#define RV1103_PERI_SOFTRST_CON5         0xff3b2a14u
#define RV1103_DMAC_CRU_BIT              (1u << 8)

#define RV1103_DMAC_CHANNEL              0u
#define RV1103_DMAC_TIMEOUT_US           100000u
#define RV1103_DMAC_PROGRAM_SIZE         128u
#define RV1103_DMAC_ALIGNMENT            32u
#define RV1103_DMAC_MAX_XFER             (1024u * 1024u)

#define PL330_STATE_MASK                 0x0fu
#define PL330_STATE_STOPPED              0x00u
#define PL330_DBG_BUSY                   (1u << 0)
#define PL330_CR0_MANAGER_NS             (1u << 2)
#define PL330_CR0_NUM_CHANNELS_SHIFT     4
#define PL330_CR0_NUM_CHANNELS_MASK      0x7u
#define PL330_CR0_NUM_EVENTS_SHIFT       17
#define PL330_CR0_NUM_EVENTS_MASK        0x1fu
#define PL330_PERIPH_ID_EXPECTED         0x41330u

#define PL330_CC_SRCINC                  (1u << 0)
#define PL330_CC_DSTINC                  (1u << 14)
#define PL330_CC_SRCNS                   (1u << 9)
#define PL330_CC_DSTNS                   (1u << 23)
#define PL330_CC_SRC_SIZE_SHIFT          1
#define PL330_CC_DST_SIZE_SHIFT          15
#define PL330_CC_SRC_LEN_SHIFT           4
#define PL330_CC_DST_LEN_SHIFT           18

#define PL330_CMD_DMAEND                 0x00u
#define PL330_CMD_DMALD                  0x04u
#define PL330_CMD_DMAST                  0x08u
#define PL330_CMD_DMARMB                 0x12u
#define PL330_CMD_DMAWMB                 0x13u
#define PL330_CMD_DMALP                  0x20u
#define PL330_CMD_DMALPEND               0x28u
#define PL330_CMD_DMAGO                  0xa0u
#define PL330_CMD_DMAMOV                 0xbcu

#define PL330_MOV_SAR                    0u
#define PL330_MOV_CCR                    1u
#define PL330_MOV_DAR                    2u

struct rv1103_dma_dev_s
{
  mutex_t lock;
  bool initialized;
  bool manager_ns;
  uint8_t program[RV1103_DMAC_PROGRAM_SIZE]
    __attribute__((aligned(RV1103_DMAC_ALIGNMENT)));
};

static struct rv1103_dma_dev_s g_rv1103_dma =
{
  .lock = NXMUTEX_INITIALIZER,
};

static void rv1103_dma_putle32(uint8_t *buf, uint32_t value)
{
  buf[0] = value;
  buf[1] = value >> 8;
  buf[2] = value >> 16;
  buf[3] = value >> 24;
}

static int rv1103_dma_emit_mov(uint8_t *buf, size_t size, size_t *offset,
                               uint8_t reg, uint32_t value)
{
  if (*offset + 6 > size)
    {
      return -E2BIG;
    }

  buf[(*offset)++] = PL330_CMD_DMAMOV;
  buf[(*offset)++] = reg;
  rv1103_dma_putle32(&buf[*offset], value);
  *offset += 4;
  return 0;
}

static int rv1103_dma_emit_body(uint8_t *buf, size_t size, size_t *offset)
{
  if (*offset + 4 > size)
    {
      return -E2BIG;
    }

  /* The barriers are required by PL330 r0p0 and are harmless on later
   * revisions.  Keeping them makes the initial driver revision-independent.
   */

  buf[(*offset)++] = PL330_CMD_DMALD;
  buf[(*offset)++] = PL330_CMD_DMARMB;
  buf[(*offset)++] = PL330_CMD_DMAST;
  buf[(*offset)++] = PL330_CMD_DMAWMB;
  return 0;
}

static int rv1103_dma_emit_loop(uint8_t *buf, size_t size, size_t *offset,
                                uint8_t lc, unsigned int count)
{
  if (count == 0 || count > 256 || *offset + 2 > size)
    {
      return -EINVAL;
    }

  buf[(*offset)++] = PL330_CMD_DMALP | ((lc & 1u) << 1);
  buf[(*offset)++] = (uint8_t)(count - 1);
  return 0;
}

static int rv1103_dma_emit_lpend(uint8_t *buf, size_t size, size_t *offset,
                                 uint8_t lc, uint8_t jump)
{
  if (*offset + 2 > size)
    {
      return -E2BIG;
    }

  buf[(*offset)++] = PL330_CMD_DMALPEND | (1u << 4) |
                     ((lc & 1u) << 2);
  buf[(*offset)++] = jump;
  return 0;
}

static int rv1103_dma_emit_bursts(uint8_t *buf, size_t size, size_t *offset,
                                  size_t bursts)
{
  size_t outer;
  size_t remainder;
  int ret;

  if (bursts > 256)
    {
      outer = bursts / 256;
      if (outer > 256)
        {
          return -E2BIG;
        }

      ret = rv1103_dma_emit_loop(buf, size, offset, 0, outer);
      if (ret < 0)
        {
          return ret;
        }

      ret = rv1103_dma_emit_loop(buf, size, offset, 1, 256);
      if (ret < 0)
        {
          return ret;
        }

      ret = rv1103_dma_emit_body(buf, size, offset);
      if (ret < 0)
        {
          return ret;
        }

      ret = rv1103_dma_emit_lpend(buf, size, offset, 1, 4);
      if (ret < 0)
        {
          return ret;
        }

      ret = rv1103_dma_emit_lpend(buf, size, offset, 0, 8);
      if (ret < 0)
        {
          return ret;
        }

      remainder = bursts % 256;
    }
  else
    {
      remainder = bursts;
    }

  if (remainder == 1)
    {
      return rv1103_dma_emit_body(buf, size, offset);
    }
  else if (remainder > 1)
    {
      ret = rv1103_dma_emit_loop(buf, size, offset, 0, remainder);
      if (ret < 0)
        {
          return ret;
        }

      ret = rv1103_dma_emit_body(buf, size, offset);
      if (ret < 0)
        {
          return ret;
        }

      return rv1103_dma_emit_lpend(buf, size, offset, 0, 4);
    }

  return 0;
}

static uint32_t rv1103_dma_ccr(unsigned int burst_len,
                               bool nonsecure)
{
  uint32_t ccr;

  /* Four-byte beats.  The main loop uses four beats per burst. */

  ccr = PL330_CC_SRCINC | PL330_CC_DSTINC |
        (2u << PL330_CC_SRC_SIZE_SHIFT) |
        (2u << PL330_CC_DST_SIZE_SHIFT) |
        ((burst_len - 1u) << PL330_CC_SRC_LEN_SHIFT) |
        ((burst_len - 1u) << PL330_CC_DST_LEN_SHIFT);

  if (nonsecure)
    {
      ccr |= PL330_CC_SRCNS | PL330_CC_DSTNS;
    }

  return ccr;
}

static int rv1103_dma_build_program(uintptr_t dst, uintptr_t src, size_t len,
                                    size_t *program_len)
{
  uint8_t *buf = g_rv1103_dma.program;
  size_t bursts = len / 16;
  size_t words = (len % 16) / 4;
  size_t offset = 0;
  int ret;

  memset(buf, 0, sizeof(g_rv1103_dma.program));

  ret = rv1103_dma_emit_mov(buf, sizeof(g_rv1103_dma.program), &offset,
                            PL330_MOV_CCR,
                            rv1103_dma_ccr(4, g_rv1103_dma.manager_ns));
  if (ret < 0)
    {
      return ret;
    }

  ret = rv1103_dma_emit_mov(buf, sizeof(g_rv1103_dma.program), &offset,
                            PL330_MOV_SAR, (uint32_t)src);
  if (ret < 0)
    {
      return ret;
    }

  ret = rv1103_dma_emit_mov(buf, sizeof(g_rv1103_dma.program), &offset,
                            PL330_MOV_DAR, (uint32_t)dst);
  if (ret < 0)
    {
      return ret;
    }

  ret = rv1103_dma_emit_bursts(buf, sizeof(g_rv1103_dma.program), &offset,
                               bursts);
  if (ret < 0)
    {
      return ret;
    }

  if (words > 0)
    {
      ret = rv1103_dma_emit_mov(buf, sizeof(g_rv1103_dma.program), &offset,
                                PL330_MOV_CCR,
                                rv1103_dma_ccr(1,
                                               g_rv1103_dma.manager_ns));
      if (ret < 0)
        {
          return ret;
        }

      while (words-- > 0)
        {
          ret = rv1103_dma_emit_body(buf, sizeof(g_rv1103_dma.program),
                                     &offset);
          if (ret < 0)
            {
              return ret;
            }
        }
    }

  if (offset >= sizeof(g_rv1103_dma.program))
    {
      return -E2BIG;
    }

  buf[offset++] = PL330_CMD_DMAEND;
  *program_len = offset;
  return 0;
}

static int rv1103_dma_wait_debug_idle(void)
{
  unsigned int elapsed;

  for (elapsed = 0; elapsed < RV1103_DMAC_TIMEOUT_US; elapsed += 10)
    {
      if ((getreg32(RV1103_DMAC_DBGSTATUS) & PL330_DBG_BUSY) == 0)
        {
          return 0;
        }

      up_udelay(10);
    }

  return -ETIMEDOUT;
}

static int rv1103_dma_start(uintptr_t program)
{
  uint8_t go0 = PL330_CMD_DMAGO |
                (g_rv1103_dma.manager_ns ? (1u << 1) : 0);
  uint8_t go1 = RV1103_DMAC_CHANNEL;
  int ret;

  ret = rv1103_dma_wait_debug_idle();
  if (ret < 0)
    {
      return ret;
    }

  putreg32(((uint32_t)go0 << 16) | ((uint32_t)go1 << 24),
           RV1103_DMAC_DBGINST0);
  putreg32((uint32_t)program, RV1103_DMAC_DBGINST1);
  UP_DSB();
  putreg32(0, RV1103_DMAC_DBGCMD);
  return 0;
}

static int rv1103_dma_wait_complete(uintptr_t dst, uintptr_t src, size_t len)
{
  unsigned int elapsed;
  uint32_t fsc;

  for (elapsed = 0; elapsed < RV1103_DMAC_TIMEOUT_US; elapsed += 10)
    {
      fsc = getreg32(RV1103_DMAC_FSC);
      if ((getreg32(RV1103_DMAC_FSM) & 1u) != 0 ||
          (fsc & (1u << RV1103_DMAC_CHANNEL)) != 0)
        {
          syslog(LOG_ERR,
                 "ERROR: PL330 fault fsm=%08lx ftm=%08lx dpc=%08lx "
                 "fsc=%08lx ftc=%08lx cpc=%08lx\n",
                 (unsigned long)getreg32(RV1103_DMAC_FSM),
                 (unsigned long)getreg32(RV1103_DMAC_FTM),
                 (unsigned long)getreg32(RV1103_DMAC_DPC),
                 (unsigned long)fsc,
                 (unsigned long)getreg32(
                   RV1103_DMAC_FTC(RV1103_DMAC_CHANNEL)),
                 (unsigned long)getreg32(
                   RV1103_DMAC_CPC(RV1103_DMAC_CHANNEL)));
          return -EIO;
        }

      if ((getreg32(RV1103_DMAC_CS(RV1103_DMAC_CHANNEL)) &
           PL330_STATE_MASK) == PL330_STATE_STOPPED &&
          getreg32(RV1103_DMAC_SA(RV1103_DMAC_CHANNEL)) == src + len &&
          getreg32(RV1103_DMAC_DA(RV1103_DMAC_CHANNEL)) == dst + len)
        {
          return 0;
        }

      up_udelay(10);
    }

  syslog(LOG_ERR,
         "ERROR: PL330 timeout ds=%08lx cs=%08lx sa=%08lx da=%08lx\n",
         (unsigned long)getreg32(RV1103_DMAC_DS),
         (unsigned long)getreg32(RV1103_DMAC_CS(RV1103_DMAC_CHANNEL)),
         (unsigned long)getreg32(RV1103_DMAC_SA(RV1103_DMAC_CHANNEL)),
         (unsigned long)getreg32(RV1103_DMAC_DA(RV1103_DMAC_CHANNEL)));
  return -ETIMEDOUT;
}

static uint32_t rv1103_dma_read_pid(void)
{
  uint32_t pid = 0;
  unsigned int i;

  for (i = 0; i < 4; i++)
    {
      pid |= (getreg32(RV1103_DMAC_PID(i)) & 0xffu) << (i * 8);
    }

  return pid;
}

static int rv1103_dma_hw_initialize(void)
{
  uint32_t channels;
  uint32_t events;
  uint32_t cr0;
  uint32_t pid;

  /* A zero Rockchip gate bit enables the clock. */

  putreg32(RV1103_DMAC_CRU_BIT << 16, RV1103_PERI_CLKGATE_CON5);
  putreg32((RV1103_DMAC_CRU_BIT << 16) | RV1103_DMAC_CRU_BIT,
           RV1103_PERI_SOFTRST_CON5);
  up_udelay(10);
  putreg32(RV1103_DMAC_CRU_BIT << 16, RV1103_PERI_SOFTRST_CON5);
  up_udelay(10);

  pid = rv1103_dma_read_pid();
  if ((pid & 0xfffffu) != PL330_PERIPH_ID_EXPECTED)
    {
      syslog(LOG_ERR, "ERROR: PL330 periph-id=%08lx expected=*41330\n",
             (unsigned long)pid);
      return -ENODEV;
    }

  cr0 = getreg32(RV1103_DMAC_CR0);
  channels = ((cr0 >> PL330_CR0_NUM_CHANNELS_SHIFT) &
              PL330_CR0_NUM_CHANNELS_MASK) + 1;
  events = ((cr0 >> PL330_CR0_NUM_EVENTS_SHIFT) &
            PL330_CR0_NUM_EVENTS_MASK) + 1;
  if (channels < 1 || events < 1)
    {
      return -ENODEV;
    }

  g_rv1103_dma.manager_ns = (cr0 & PL330_CR0_MANAGER_NS) != 0;

  /* Polling mode: mask every event and discard stale status. */

  putreg32(0, RV1103_DMAC_INTEN);
  putreg32(0xffu, RV1103_DMAC_INTCLR);

  if ((getreg32(RV1103_DMAC_DS) & PL330_STATE_MASK) !=
      PL330_STATE_STOPPED)
    {
      return -EBUSY;
    }

  syslog(LOG_INFO,
         "PL330: id=%08lx channels=%lu events=%lu "
         "width=%lu-bit manager=%s\n",
         (unsigned long)pid, (unsigned long)channels, (unsigned long)events,
         (unsigned long)(8u << (getreg32(RV1103_DMAC_CRD) & 7u)),
         g_rv1103_dma.manager_ns ? "non-secure" : "secure");
  return 0;
}

int rv1103_dma_memcpy(void *dst, const void *src, size_t len)
{
  size_t program_len;
  uintptr_t dstaddr = (uintptr_t)dst;
  uintptr_t srcaddr = (uintptr_t)src;
  int ret;

  if (!g_rv1103_dma.initialized)
    {
      return -ENODEV;
    }

  if (dst == NULL || src == NULL || len == 0 ||
      len > RV1103_DMAC_MAX_XFER ||
      ((dstaddr | srcaddr | len) & 3u) != 0)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&g_rv1103_dma.lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = rv1103_dma_build_program(dstaddr, srcaddr, len, &program_len);
  if (ret < 0)
    {
      goto out;
    }

  /* The CPU and PL330 are not hardware-coherent on RV1103.  Clean the
   * source and program, and clean+invalidate the complete destination range
   * before handing ownership to DMA.  The caller receives an invalidated
   * destination after completion.
   */

  up_clean_dcache(srcaddr, srcaddr + len);
  up_flush_dcache(dstaddr, dstaddr + len);
  up_clean_dcache((uintptr_t)g_rv1103_dma.program,
                  (uintptr_t)g_rv1103_dma.program + program_len);
  UP_DSB();

  ret = rv1103_dma_start((uintptr_t)g_rv1103_dma.program);
  if (ret == 0)
    {
      ret = rv1103_dma_wait_complete(dstaddr, srcaddr, len);
    }

  UP_DSB();
  up_invalidate_dcache(dstaddr, dstaddr + len);

out:
  nxmutex_unlock(&g_rv1103_dma.lock);
  return ret;
}

static int rv1103_dma_selftest(void)
{
  const size_t guard = RV1103_DMAC_ALIGNMENT;
  const size_t length = 4096;
  const size_t total = length + guard * 2;
  uint8_t *src;
  uint8_t *dst;
  size_t i;
  int ret = -ENOMEM;

  src = kmm_memalign(RV1103_DMAC_ALIGNMENT, total);
  dst = kmm_memalign(RV1103_DMAC_ALIGNMENT, total);
  if (src == NULL || dst == NULL)
    {
      goto out;
    }

  memset(src, 0x3c, total);
  memset(dst, 0xa5, total);
  for (i = 0; i < length; i++)
    {
      src[guard + i] = (uint8_t)((i * 37u + (i >> 3) + 0x5au) & 0xffu);
    }

  ret = rv1103_dma_memcpy(dst + guard, src + guard, length);
  if (ret < 0)
    {
      goto out;
    }

  if (memcmp(dst + guard, src + guard, length) != 0)
    {
      ret = -EILSEQ;
      goto out;
    }

  for (i = 0; i < guard; i++)
    {
      if (dst[i] != 0xa5 || dst[guard + length + i] != 0xa5 ||
          src[i] != 0x3c || src[guard + length + i] != 0x3c)
        {
          ret = -EOVERFLOW;
          goto out;
        }
    }

  syslog(LOG_INFO,
         "PL330: 4096-byte memcpy self-test passed (cache + guards)\n");

out:
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: PL330 memcpy self-test failed: %d\n", ret);
    }

  kmm_free(dst);
  kmm_free(src);
  return ret;
}

int rv1103_dma_initialize(void)
{
  int ret;

  if (g_rv1103_dma.initialized)
    {
      return 0;
    }

  ret = rv1103_dma_hw_initialize();
  if (ret < 0)
    {
      return ret;
    }

  g_rv1103_dma.initialized = true;
  ret = rv1103_dma_selftest();
  if (ret < 0)
    {
      g_rv1103_dma.initialized = false;
    }

  return ret;
}

#endif /* CONFIG_RV1103_PL330_DMA */
