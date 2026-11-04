/****************************************************************************
 * vendor/allwinnertech/chips/a733/a733_sdmmc.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/* A deliberately conservative PIO lower half for sun60iw2p1 SMHC0.  The
 * official boot chain has already powered the slot.  We take ownership of
 * the controller, use the 24 MHz oscillator and expose it through NuttX's
 * normal SDIO/MMCSD interface.  DMA and UHS timing are later checkpoints.
 */

#include <nuttx/config.h>

#ifdef CONFIG_A733_SDMMC0

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/clock.h>
#include <nuttx/mmcsd.h>
#include <nuttx/mutex.h>
#include <nuttx/sdio.h>
#include <nuttx/wqueue.h>

#include "arm64_internal.h"

#define A733_CCU_BASE          UINT64_C(0x02001000)
#define A733_SMHC0_CLK         (A733_CCU_BASE + 0x0d00)
#define A733_SMHC0_BGR         (A733_CCU_BASE + 0x0d0c)
#define A733_SMHC0_BASE        UINT64_C(0x04020000)

#define SMHC_GCTRL             0x000
#define SMHC_CLKCR             0x004
#define SMHC_TIMEOUT           0x008
#define SMHC_WIDTH             0x00c
#define SMHC_BLKSZ             0x010
#define SMHC_BYTECNT           0x014
#define SMHC_CMD               0x018
#define SMHC_ARG               0x01c
#define SMHC_RESP0             0x020
#define SMHC_RESP1             0x024
#define SMHC_RESP2             0x028
#define SMHC_RESP3             0x02c
#define SMHC_IMASK             0x030
#define SMHC_RINT              0x038
#define SMHC_STATUS            0x03c
#define SMHC_FTRGL             0x040
#define SMHC_FIFO              0x200

#define GCTRL_RESET            (1u << 0)
#define GCTRL_FIFO_RESET       (1u << 1)
#define GCTRL_DMA_RESET        (1u << 2)
#define GCTRL_AHB_ACCESS       (1u << 31)

#define CLKCR_DIV_MASK         0xffu
#define CLKCR_ENABLE           (1u << 16)

#define CMD_RESP_EXPECT        (1u << 6)
#define CMD_RESP_LONG          (1u << 7)
#define CMD_RESP_CRC           (1u << 8)
#define CMD_DATA_EXPECT        (1u << 9)
#define CMD_WRITE              (1u << 10)
#define CMD_WAIT_PREV          (1u << 13)
#define CMD_STOP_ABORT         (1u << 14)
#define CMD_SEND_INIT          (1u << 15)
#define CMD_UPCLK_ONLY         (1u << 21)
#define CMD_START              (1u << 31)

#define RINT_RESP_ERR          (1u << 1)
#define RINT_CMD_DONE          (1u << 2)
#define RINT_DATA_OVER         (1u << 3)
#define RINT_RESP_CRC          (1u << 6)
#define RINT_DATA_CRC          (1u << 7)
#define RINT_RESP_TIMEOUT      (1u << 8)
#define RINT_DATA_TIMEOUT      (1u << 9)
#define RINT_FIFO_ERROR        (1u << 11)
#define RINT_ERROR_MASK        (RINT_RESP_ERR | RINT_RESP_CRC | \
                                RINT_DATA_CRC | RINT_RESP_TIMEOUT | \
                                RINT_DATA_TIMEOUT | RINT_FIFO_ERROR)

#define STATUS_FIFO_EMPTY      (1u << 2)
#define STATUS_FIFO_FULL       (1u << 3)

#define A733_CMD_TIMEOUT       MSEC2TICK(500)
#define A733_DATA_TIMEOUT      MSEC2TICK(5000)

struct a733_sdmmc_s
{
  struct sdio_dev_s dev;
  uint8_t          *buffer;
  size_t            remaining;
  unsigned int      blocklen;
  unsigned int      nblocks;
  bool              write;
  int               result;
  sdio_eventset_t   waitevents;
  worker_t          callback;
  void             *cbarg;
};

static inline uint32_t smhc_get(unsigned int offset)
{
  return getreg32(A733_SMHC0_BASE + offset);
}

static inline void smhc_put(unsigned int offset, uint32_t value)
{
  putreg32(value, A733_SMHC0_BASE + offset);
}

static int a733_wait_clear(unsigned int offset, uint32_t mask,
                           clock_t timeout)
{
  clock_t start = clock_systime_ticks();

  while ((smhc_get(offset) & mask) != 0)
    {
      if (clock_systime_ticks() - start > timeout)
        {
          return -ETIMEDOUT;
        }
    }

  return OK;
}

static int a733_update_clock(uint32_t divider)
{
  uint32_t value;
  clock_t start;

  value = smhc_get(SMHC_CLKCR);
  value &= ~CLKCR_ENABLE;
  smhc_put(SMHC_CLKCR, value);
  smhc_put(SMHC_ARG, 0);
  smhc_put(SMHC_CMD, CMD_START | CMD_UPCLK_ONLY | CMD_WAIT_PREV);
  if (a733_wait_clear(SMHC_CMD, CMD_START, A733_CMD_TIMEOUT) < 0)
    {
      return -ETIMEDOUT;
    }

  value = (value & ~CLKCR_DIV_MASK) | (divider & CLKCR_DIV_MASK) |
          CLKCR_ENABLE;
  smhc_put(SMHC_CLKCR, value);
  smhc_put(SMHC_ARG, 0);
  smhc_put(SMHC_CMD, CMD_START | CMD_UPCLK_ONLY | CMD_WAIT_PREV);
  start = clock_systime_ticks();
  while ((smhc_get(SMHC_CMD) & CMD_START) != 0)
    {
      if (clock_systime_ticks() - start > A733_CMD_TIMEOUT)
        {
          return -ETIMEDOUT;
        }
    }

  smhc_put(SMHC_RINT, 0xffffffffu);
  return OK;
}

static void a733_reset(struct sdio_dev_s *dev)
{
  uint32_t value;

  /* Select xin24m, enable module clock and deassert only SMHC0 reset. */

  putreg32(1u | (1u << 16), A733_SMHC0_BGR);
  putreg32(1u << 31, A733_SMHC0_CLK);

  smhc_put(SMHC_GCTRL, GCTRL_RESET | GCTRL_FIFO_RESET | GCTRL_DMA_RESET);
  if (a733_wait_clear(SMHC_GCTRL,
                      GCTRL_RESET | GCTRL_FIFO_RESET | GCTRL_DMA_RESET,
                      A733_CMD_TIMEOUT) < 0)
    {
      syslog(LOG_ERR, "A733 SDMMC0: controller reset timeout\n");
      return;
    }

  smhc_put(SMHC_GCTRL, GCTRL_AHB_ACCESS);
  smhc_put(SMHC_TIMEOUT, 0xffffffffu);
  smhc_put(SMHC_WIDTH, 0);
  smhc_put(SMHC_IMASK, 0);
  smhc_put(SMHC_RINT, 0xffffffffu);
  smhc_put(SMHC_FTRGL, (7u << 16) | 8u);
  value = smhc_get(SMHC_CLKCR) & ~(CLKCR_DIV_MASK | CLKCR_ENABLE);
  smhc_put(SMHC_CLKCR, value);
  (void)a733_update_clock(29); /* 24 MHz / 2 / (29 + 1) = 400 kHz */
}

static sdio_capset_t a733_capabilities(struct sdio_dev_s *dev)
{
  /* Despite the historical capability name, this bit tells the MMC/SD
   * upper half that the transfer buffer must be supplied before CMD24/25.
   * That ordering is required for both PIO FIFO priming and future IDMA.
   */

  return SDIO_CAPS_4BIT | SDIO_CAPS_DMABEFOREWRITE;
}

static sdio_statset_t a733_status(struct sdio_dev_s *dev)
{
  /* This board boots this image from the same non-removable-at-runtime slot.
   * PF6 card-detect support will be added with hotplug/IRQ handling.
   */

  return SDIO_STATUS_PRESENT;
}

static void a733_widebus(struct sdio_dev_s *dev, bool enable)
{
  smhc_put(SMHC_WIDTH, enable ? 1u : 0u);
}

static void a733_clock(struct sdio_dev_s *dev, enum sdio_clock_e rate)
{
  if (rate == CLOCK_SDIO_DISABLED)
    {
      smhc_put(SMHC_CLKCR, smhc_get(SMHC_CLKCR) & ~CLKCR_ENABLE);
    }
  else if (rate == CLOCK_IDMODE)
    {
      (void)a733_update_clock(29);
    }
  else
    {
      /* 12 MHz is intentionally below high-speed mode for the PIO stage. */

      (void)a733_update_clock(0);
    }
}

static int a733_attach(struct sdio_dev_s *dev)
{
  return OK;
}

static int a733_transfer(struct a733_sdmmc_s *priv)
{
  clock_t start = clock_systime_ticks();
  uint32_t status;
  uint32_t ints;
  uint32_t word;
  size_t chunk;

  while (priv->remaining > 0)
    {
      ints = smhc_get(SMHC_RINT);
      if ((ints & RINT_ERROR_MASK) != 0)
        {
          syslog(LOG_ERR, "A733 SDMMC0: data error rint=%08lx status=%08lx\n",
                 (unsigned long)ints, (unsigned long)smhc_get(SMHC_STATUS));
          return (ints & (RINT_RESP_TIMEOUT | RINT_DATA_TIMEOUT)) != 0 ?
                 -ETIMEDOUT : -EIO;
        }

      status = smhc_get(SMHC_STATUS);
      if (!priv->write && (status & STATUS_FIFO_EMPTY) == 0)
        {
          word = smhc_get(SMHC_FIFO);
          chunk = priv->remaining < sizeof(word) ? priv->remaining :
                                                  sizeof(word);
          memcpy(priv->buffer, &word, chunk);
          priv->buffer += chunk;
          priv->remaining -= chunk;
        }
      else if (priv->write && (status & STATUS_FIFO_FULL) == 0)
        {
          word = 0;
          chunk = priv->remaining < sizeof(word) ? priv->remaining :
                                                  sizeof(word);
          memcpy(&word, priv->buffer, chunk);
          smhc_put(SMHC_FIFO, word);
          priv->buffer += chunk;
          priv->remaining -= chunk;
        }

      if (clock_systime_ticks() - start > A733_DATA_TIMEOUT)
        {
          return -ETIMEDOUT;
        }
    }

  start = clock_systime_ticks();
  for (;;)
    {
      ints = smhc_get(SMHC_RINT);
      if ((ints & RINT_ERROR_MASK) != 0)
        {
          return (ints & (RINT_RESP_TIMEOUT | RINT_DATA_TIMEOUT)) != 0 ?
                 -ETIMEDOUT : -EIO;
        }

      if ((ints & RINT_DATA_OVER) != 0)
        {
          return OK;
        }

      if (clock_systime_ticks() - start > A733_DATA_TIMEOUT)
        {
          return -ETIMEDOUT;
        }
    }
}

static int a733_sendcmd(struct sdio_dev_s *dev, uint32_t cmd, uint32_t arg)
{
  struct a733_sdmmc_s *priv = (struct a733_sdmmc_s *)dev;
  uint32_t command = (cmd & MMCSD_CMDIDX_MASK) >> MMCSD_CMDIDX_SHIFT;
  uint32_t response = cmd & MMCSD_RESPONSE_MASK;
  bool data = (cmd & MMCSD_DATAXFR_MASK) != 0;
  clock_t start;
  uint32_t ints;

  priv->result = OK;

  if (response != MMCSD_NO_RESPONSE)
    {
      command |= CMD_RESP_EXPECT;
      if (response == MMCSD_R2_RESPONSE)
        {
          command |= CMD_RESP_LONG;
        }

      if (response != MMCSD_R3_RESPONSE && response != MMCSD_R4_RESPONSE)
        {
          command |= CMD_RESP_CRC;
        }
    }

  if (data)
    {
      command |= CMD_DATA_EXPECT | CMD_WAIT_PREV;
      if ((cmd & MMCSD_WRXFR) != 0)
        {
          command |= CMD_WRITE;
        }

      smhc_put(SMHC_BLKSZ, priv->blocklen);
      smhc_put(SMHC_BYTECNT, priv->blocklen * priv->nblocks);
    }

  if (((cmd & MMCSD_CMDIDX_MASK) >> MMCSD_CMDIDX_SHIFT) == 0)
    {
      command |= CMD_SEND_INIT;
    }

  if ((cmd & MMCSD_STOPXFR) != 0)
    {
      command |= CMD_STOP_ABORT;
    }

  smhc_put(SMHC_RINT, 0xffffffffu);
  smhc_put(SMHC_ARG, arg);
  smhc_put(SMHC_CMD, command | CMD_START);

  priv->result = data ? a733_transfer(priv) : OK;
  if (priv->result < 0)
    {
      return priv->result;
    }

  start = clock_systime_ticks();
  while (((ints = smhc_get(SMHC_RINT)) & RINT_CMD_DONE) == 0)
    {
      if ((ints & RINT_ERROR_MASK) != 0)
        {
          return (ints & RINT_RESP_TIMEOUT) != 0 ? -ETIMEDOUT : -EIO;
        }

      if (clock_systime_ticks() - start > A733_CMD_TIMEOUT)
        {
          return -ETIMEDOUT;
        }
    }

  return OK;
}

#ifdef CONFIG_SDIO_BLOCKSETUP
static void a733_blocksetup(struct sdio_dev_s *dev, unsigned int blocklen,
                            unsigned int nblocks)
{
  struct a733_sdmmc_s *priv = (struct a733_sdmmc_s *)dev;
  priv->blocklen = blocklen;
  priv->nblocks = nblocks;
}
#endif

static int a733_recvsetup(struct sdio_dev_s *dev, uint8_t *buffer,
                          size_t nbytes)
{
  struct a733_sdmmc_s *priv = (struct a733_sdmmc_s *)dev;
  smhc_put(SMHC_GCTRL, smhc_get(SMHC_GCTRL) | GCTRL_FIFO_RESET);
  if (a733_wait_clear(SMHC_GCTRL, GCTRL_FIFO_RESET, A733_CMD_TIMEOUT) < 0)
    {
      return -ETIMEDOUT;
    }

  priv->buffer = buffer;
  priv->remaining = nbytes;
  priv->write = false;
  return OK;
}

static int a733_sendsetup(struct sdio_dev_s *dev, const uint8_t *buffer,
                          size_t nbytes)
{
  struct a733_sdmmc_s *priv = (struct a733_sdmmc_s *)dev;
  smhc_put(SMHC_GCTRL, smhc_get(SMHC_GCTRL) | GCTRL_FIFO_RESET);
  if (a733_wait_clear(SMHC_GCTRL, GCTRL_FIFO_RESET, A733_CMD_TIMEOUT) < 0)
    {
      return -ETIMEDOUT;
    }

  priv->buffer = (uint8_t *)buffer;
  priv->remaining = nbytes;
  priv->write = true;
  return OK;
}

static int a733_cancel(struct sdio_dev_s *dev)
{
  struct a733_sdmmc_s *priv = (struct a733_sdmmc_s *)dev;
  priv->remaining = 0;
  smhc_put(SMHC_RINT, 0xffffffffu);
  return OK;
}

static int a733_waitresponse(struct sdio_dev_s *dev, uint32_t cmd)
{
  struct a733_sdmmc_s *priv = (struct a733_sdmmc_s *)dev;
  return priv->result;
}

static int a733_recvshort(struct sdio_dev_s *dev, uint32_t cmd,
                          uint32_t *response)
{
  uint32_t ints = smhc_get(SMHC_RINT);
  if ((ints & RINT_RESP_TIMEOUT) != 0)
    {
      return -ETIMEDOUT;
    }

  if ((cmd & MMCSD_RESPONSE_MASK) != MMCSD_R3_RESPONSE &&
      (ints & RINT_RESP_CRC) != 0)
    {
      return -EIO;
    }

  *response = smhc_get(SMHC_RESP0);
  return OK;
}

static int a733_recvlong(struct sdio_dev_s *dev, uint32_t cmd,
                         uint32_t response[4])
{
  uint32_t ints = smhc_get(SMHC_RINT);
  if ((ints & RINT_RESP_TIMEOUT) != 0)
    {
      return -ETIMEDOUT;
    }

  if ((ints & RINT_RESP_CRC) != 0)
    {
      return -EIO;
    }

  response[0] = smhc_get(SMHC_RESP3);
  response[1] = smhc_get(SMHC_RESP2);
  response[2] = smhc_get(SMHC_RESP1);
  response[3] = smhc_get(SMHC_RESP0);
  return OK;
}

static void a733_waitenable(struct sdio_dev_s *dev,
                            sdio_eventset_t eventset, uint32_t timeout)
{
  struct a733_sdmmc_s *priv = (struct a733_sdmmc_s *)dev;
  priv->waitevents = eventset;
}

static sdio_eventset_t a733_eventwait(struct sdio_dev_s *dev)
{
  struct a733_sdmmc_s *priv = (struct a733_sdmmc_s *)dev;
  sdio_eventset_t event = priv->result < 0 ? SDIOWAIT_ERROR :
                          SDIOWAIT_TRANSFERDONE;
  priv->waitevents = 0;
  return event;
}

static void a733_callbackenable(struct sdio_dev_s *dev,
                                sdio_eventset_t eventset)
{
}

#if defined(CONFIG_SCHED_WORKQUEUE) && defined(CONFIG_SCHED_HPWORK)
static int a733_registercallback(struct sdio_dev_s *dev, worker_t callback,
                                 void *arg)
{
  struct a733_sdmmc_s *priv = (struct a733_sdmmc_s *)dev;
  priv->callback = callback;
  priv->cbarg = arg;
  return OK;
}
#endif

static void a733_gotextcsd(struct sdio_dev_s *dev, const uint8_t *buffer)
{
}

static struct a733_sdmmc_s g_sdmmc =
{
  .dev =
  {
    .mutex            = NXMUTEX_INITIALIZER,
    .reset            = a733_reset,
    .capabilities     = a733_capabilities,
    .status           = a733_status,
    .widebus          = a733_widebus,
    .clock            = a733_clock,
    .attach           = a733_attach,
    .sendcmd          = a733_sendcmd,
#ifdef CONFIG_SDIO_BLOCKSETUP
    .blocksetup       = a733_blocksetup,
#endif
    .recvsetup        = a733_recvsetup,
    .sendsetup        = a733_sendsetup,
    .cancel           = a733_cancel,
    .waitresponse     = a733_waitresponse,
    .recv_r1          = a733_recvshort,
    .recv_r2          = a733_recvlong,
    .recv_r3          = a733_recvshort,
    .recv_r4          = a733_recvshort,
    .recv_r5          = a733_recvshort,
    .recv_r6          = a733_recvshort,
    .recv_r7          = a733_recvshort,
    .waitenable       = a733_waitenable,
    .eventwait        = a733_eventwait,
    .callbackenable   = a733_callbackenable,
#if defined(CONFIG_SCHED_WORKQUEUE) && defined(CONFIG_SCHED_HPWORK)
    .registercallback = a733_registercallback,
#endif
    .gotextcsd        = a733_gotextcsd,
  },
  .blocklen = 512,
  .nblocks = 1,
};

int a733_sdmmc0_initialize(void)
{
  int ret;

  a733_reset(&g_sdmmc.dev);
  ret = mmcsd_slotinitialize(0, &g_sdmmc.dev);
  if (ret < 0)
    {
      return ret;
    }

  syslog(LOG_INFO,
         "A733 SDMMC0: /dev/mmcsd0 requested (PIO, 400 kHz/12 MHz, 4-bit)\n");
  return OK;
}

#endif /* CONFIG_A733_SDMMC0 */
