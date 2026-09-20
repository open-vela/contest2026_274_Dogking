/****************************************************************************
 * vendor/rockchip/chips/rk3506/rk3506_sdmmc.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <debug.h>
#include <errno.h>
#include <syslog.h>

#include <nuttx/wdog.h>
#include <nuttx/clock.h>
#include <nuttx/arch.h>
#include <nuttx/cache.h>
#include <nuttx/sdio.h>
#include <nuttx/wqueue.h>
#include <nuttx/semaphore.h>
#include <nuttx/mmcsd.h>
#include <nuttx/spinlock.h>

#include <nuttx/irq.h>

#include <arch/chip/rk3506_sdmmc.h>

#include "arm_internal.h"

#include <arch/board/board.h>

#ifdef CONFIG_RK3506_SDMMC

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MCI_DMADES0_OWN         (1UL << 31)
#define MCI_DMADES0_CH          (1 << 4)
#define MCI_DMADES0_FS          (1 << 3)
#define MCI_DMADES0_LD          (1 << 2)
#define MCI_DMADES0_DIC         (1 << 1)
#define MCI_DMADES1_MAXTR       4096
#define MCI_DMADES1_BS1(x)      (x)

/* RK3506 integration registers.  U-Boot already selected a 52 MHz SDMMC
 * source clock.  The first openvela storage stage deliberately preserves
 * that source and only ungates the clocks, configures the SD pins and uses
 * the controller's own divider.
 */

#define RK3506_CRU_BASE                  0xff9a0000u
#define RK3506_CRU_GATE_CON17            (RK3506_CRU_BASE + 0x0844u)
#define RK3506_CRU_GATE_CON19            (RK3506_CRU_BASE + 0x084cu)
#define RK3506_SDMMC_CLOCK_GATES         ((1u << 6) | (1u << 7))
#define RK3506_GPIO234_IOC_GATE          (1u << 8)

#define RK3506_GPIO3_IOC_BASE            0xff4d8000u
#define RK3506_GPIO3A_IOMUX_SEL_0        \
  (RK3506_GPIO3_IOC_BASE + 0x0060u)
#define RK3506_GPIO3A_IOMUX_SEL_1        \
  (RK3506_GPIO3_IOC_BASE + 0x0064u)
#define RK3506_GPIO3A_PULL               \
  (RK3506_GPIO3_IOC_BASE + 0x0230u)

#define RK3506_WRITE_MASK(mask, value)   \
  (((uint32_t)(mask) << 16) | ((uint32_t)(value) & (mask)))

/* Configuration ************************************************************/

/* Required system configuration options in the sched/Kconfig:
 *
 *   CONFIG_SCHED_WORKQUEUE -- Callback support requires work queue support.
 *
 * Driver-specific configuration options in the drivers/mmcd Kdonfig:
 *
 *   CONFIG_SDIO_MUXBUS - Setting this configuration enables some locking
 *     APIs to manage concurrent accesses on the SD card bus.  This is not
 *     needed for the simple case of a single SD card slot, for example.
 *   CONFIG_SDIO_WIDTH_D1_ONLY - This may be selected to force the driver
 *     operate with only a single data line (the default is to use all
 *     4 SD data lines).
 *   CONFIG_MMCSD_HAVE_CARDDETECT - Select if the SD slot supports a card
 *     detect pin.
 *   CONFIG_MMCSD_HAVE_WRITEPROTECT - Select if the SD slots supports a
 *     write protected pin.
 *
 * Driver-specific configuration options in the arch/arm/src/rk3506xx/Kconfig
 *
 *   CONFIG_RK3506_SDMMC_PWRCTRL - Select if the board supports an output
 *     pin to enable power to the SD slot.
 *   CONFIG_RK3506_SDMMC_DMA - Enable SD card DMA.  This is a marginally
 *     optional.  For most usages, SD accesses will cause data overruns if
 *     used without DMA.  This will also select CONFIG_SDIO_DMA.
 *   CONFIG_RK3506_SDMMC_REGDEBUG - Enables some very low-level debug output
 *     This also requires CONFIG_DEBUG_MEMCARD_INFO
 */

#ifndef CONFIG_SCHED_WORKQUEUE
#  error "Callback support requires CONFIG_SCHED_WORKQUEUE"
#endif

#ifndef CONFIG_SDIO_BLOCKSETUP
#  error "Driver requires CONFIG_SDIO_BLOCKSETUP to be set"
#endif

/* Timing : 100mS short timeout, 2 seconds for long one */

#define SDCARD_CMDTIMEOUT       MSEC2TICK(100)
#define SDCARD_LONGTIMEOUT      MSEC2TICK(2000)
#define RK3506_RESET_TIMEOUT_US  100000

/* Type of Card Bus Size */

#define SDCARD_BUS_D1           0
#define SDCARD_BUS_D4           1
#define SDCARD_BUS_D8           0x100

/* FIFO size in bytes */

#define RK3506_TXFIFO_SIZE       (RK3506_TXFIFO_DEPTH * RK3506_TXFIFO_WIDTH)
#define RK3506_RXFIFO_SIZE       (RK3506_RXFIFO_DEPTH * RK3506_RXFIFO_WIDTH)

/* Number of DMA Descriptors */

#define NUM_DMA_DESCRIPTORS      16
#define RK3506_IDMAC_MAXSIZE     \
  (NUM_DMA_DESCRIPTORS * MCI_DMADES1_MAXTR)
#define RK3506_IDMAC_MINSIZE     512
#define RK3506_DCACHE_LINESIZE   32
#define RK3506_DMA_TRACE_READ    (1 << 0)
#define RK3506_DMA_TRACE_WRITE   (1 << 1)
#define RK3506_DMA_TRACE_REUSE   (1 << 2)

/* Data transfer interrupt mask bits */

#define SDCARD_RECV_MASK        (SDMMC_INT_DTO  | SDMMC_INT_DCRC | SDMMC_INT_DRTO | \
                                 SDMMC_INT_EBE  | SDMMC_INT_RXDR | SDMMC_INT_SBE | \
                                 SDMMC_INT_FRUN)
#define SDCARD_SEND_MASK        (SDMMC_INT_DTO  | SDMMC_INT_DCRC | SDMMC_INT_DRTO | \
                                 SDMMC_INT_EBE  | SDMMC_INT_TXDR | SDMMC_INT_SBE | \
                                 SDMMC_INT_FRUN)

#define SDCARD_DMARECV_MASK     (SDMMC_INT_DTO  | SDMMC_INT_DCRC | SDMMC_INT_DRTO | \
                                 SDMMC_INT_SBE  | SDMMC_INT_EBE)
#define SDCARD_DMASEND_MASK     (SDMMC_INT_DTO  | SDMMC_INT_DCRC | SDMMC_INT_DRTO | \
                                 SDMMC_INT_EBE)

#define SDCARD_DMAERROR_MASK    (SDMMC_IDINTEN_FBE | SDMMC_IDINTEN_DU | \
                                 SDMMC_IDINTEN_CES | SDMMC_IDINTEN_AIS)

#define SDCARD_TRANSFER_ALL     (SDMMC_INT_DTO  | SDMMC_INT_DCRC | SDMMC_INT_DRTO | \
                                 SDMMC_INT_EBE  | SDMMC_INT_TXDR | SDMMC_INT_RXDR | \
                                 SDMMC_INT_SBE  | SDMMC_INT_FRUN)

/* Event waiting interrupt mask bits */

#define SDCARD_INT_RESPERR      (SDMMC_INT_RE   | SDMMC_INT_RCRC | SDMMC_INT_RTO)

#ifdef CONFIG_MMCSD_HAVE_CARDDETECT
#  define SDCARD_INT_CDET        SDMMC_INT_CDET
#else
#  define SDCARD_INT_CDET        0
#endif

#define SDCARD_CMDDONE_STA      (SDMMC_INT_CDONE)
#define SDCARD_RESPDONE_STA     (0)

#define SDCARD_CMDDONE_MASK     (SDMMC_INT_CDONE)
#define SDCARD_RESPDONE_MASK    (SDMMC_INT_CDONE | SDCARD_INT_RESPERR)
#define SDCARD_XFRDONE_MASK     (0)  /* Handled by transfer masks */

#define SDCARD_CMDDONE_CLEAR    (SDMMC_INT_CDONE)
#define SDCARD_RESPDONE_CLEAR   (SDMMC_INT_CDONE | SDCARD_INT_RESPERR)

#define SDCARD_XFRDONE_CLEAR    (SDCARD_TRANSFER_ALL)

#define SDCARD_WAITALL_CLEAR    (SDCARD_CMDDONE_CLEAR | SDCARD_RESPDONE_CLEAR | \
                                 SDCARD_XFRDONE_CLEAR)

/* Let's wait until we have both SD card transfer complete and DMA
 * complete.
 */

#define SDCARD_XFRDONE_FLAG     (1)
#define SDCARD_DMADONE_FLAG     (2)
#define SDCARD_ALLDONE          (3)

/* Card debounce time.  Number of host clocks (SD_CLK) used by debounce
 * filter logic for card detect.  typical debounce time is 5-25 ms.
 *
 * Eg. Fsd = 44MHz, ticks = 660,000
 */

#define DEBOUNCE_TICKS          (15 * (BOARD_SDMMC_FREQUENCY / 1000))

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct sdmmc_dma_s
{
  volatile uint32_t des0;        /* Control and status */
  volatile uint32_t des1;        /* Buffer size(s) */
  volatile uint32_t des2;        /* Buffer address pointer 1 */
  volatile uint32_t des3;        /* Buffer address pointer 2 */
};

/* This structure defines the state of the RK3506XX SD card interface */

struct rk3506_dev_s
{
  struct sdio_dev_s  dev;             /* Standard, base SD card interface */

  /* RK3506XX-specific extensions */

  /* Event support */

  sem_t              waitsem;         /* Implements event waiting */
  sdio_eventset_t    waitevents;      /* Set of events to be waited for */
  uint32_t           waitmask;        /* Interrupt enables for event waiting */
  volatile sdio_eventset_t wkupevent; /* The event that caused the wakeup */
  struct wdog_s      waitwdog;        /* Watchdog that handles event timeouts */

  /* Callback support */

  sdio_statset_t     cdstatus;        /* Card status */
  sdio_eventset_t    cbevents;        /* Set of events to be cause callbacks */
  worker_t           callback;        /* Registered callback function */
  void              *cbarg;           /* Registered callback argument */
  struct work_s      cbwork;          /* Callback work queue structure */

  /* Interrupt mode data transfer support */

  uint32_t          *buffer;          /* Address of current R/W buffer */
  uint32_t           xfrmask;         /* Interrupt enables for data transfer */
#ifdef CONFIG_RK3506_SDMMC_DMA
  uint32_t           dmamask;         /* Interrupt enables for DMA transfer */
#endif
  ssize_t            remaining;       /* Number of bytes remaining in the
                                       * transfer */
  bool               wrdir;           /* True: Writing False: Reading */
  bool               resetok;         /* Controller reset completed */

  /* DMA data transfer support */

  bool               widebus;         /* Current SD data-bus width */
#ifdef CONFIG_RK3506_SDMMC_DMA
  bool               dmamode;         /* true: DMA mode transfer */
  bool               dmabounce;       /* true: internal aligned buffer used */
  bool               dmafastready;    /* Last DMA completed without error */
  uint8_t            *dmabuffer;       /* Buffer visible to the IDMAC */
  size_t              dmalen;          /* Length of the active DMA transfer */
  size_t              dmamaxread;      /* Largest observed DMA read */
  size_t              dmamaxwrite;     /* Largest observed DMA write */
  uint8_t             dmatrace;        /* One-time read/write trace flags */
#endif
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_RK3506_SDMMC_REGDEBUG
static uint32_t rk3506_getreg(uint32_t addr);
static void rk3506_putreg(uint32_t val, uint32_t addr);
#else
#  define rk3506_getreg(addr)     getreg32(addr)
#  define rk3506_putreg(val,addr) putreg32(val,addr)
#endif

/* Low-level helpers ********************************************************/

static inline void rk3506_setclock(uint32_t clkdiv);
static inline void rk3506_sdcard_clock(bool enable);
static int  rk3506_ciu_sendcmd(uint32_t cmd, uint32_t arg);
static bool rk3506_wait_clear(uint32_t addr, uint32_t mask,
                              const char *name);
static void rk3506_enable_ints(struct rk3506_dev_s *priv);
static void rk3506_disable_allints(struct rk3506_dev_s *priv);
static void rk3506_config_waitints(struct rk3506_dev_s *priv,
              uint32_t waitmask, sdio_eventset_t waitevents,
              sdio_eventset_t wkupevents);
static void rk3506_config_xfrints(struct rk3506_dev_s *priv, uint32_t xfrmask);
#ifdef CONFIG_RK3506_SDMMC_DMA
static void rk3506_config_dmaints(struct rk3506_dev_s *priv, uint32_t xfrmask,
                                 uint32_t dmamask);
#endif

/* Data Transfer Helpers ****************************************************/

static void rk3506_eventtimeout(wdparm_t arg);
static void rk3506_endwait(struct rk3506_dev_s *priv,
              sdio_eventset_t wkupevent);
static void rk3506_endtransfer(struct rk3506_dev_s *priv,
              sdio_eventset_t wkupevent);

/* Interrupt Handling *******************************************************/

static int  rk3506_sdmmc_interrupt(int irq, void *context, void *arg);

/* SD Card Interface Methods ************************************************/

/* Mutual exclusion */

#ifdef CONFIG_SDIO_MUXBUS
static int  rk3506_lock(struct sdio_dev_s *dev, bool lock);
#endif

/* Initialization/setup */

static void rk3506_reset(struct sdio_dev_s *dev);
static sdio_capset_t rk3506_capabilities(struct sdio_dev_s *dev);
static uint8_t rk3506_status(struct sdio_dev_s *dev);
static void rk3506_widebus(struct sdio_dev_s *dev, bool enable);
static void rk3506_clock(struct sdio_dev_s *dev,
              enum sdio_clock_e rate);
static int  rk3506_attach(struct sdio_dev_s *dev);

/* Command/Status/Data Transfer */

static int  rk3506_sendcmd(struct sdio_dev_s *dev, uint32_t cmd,
              uint32_t arg);
#ifdef CONFIG_SDIO_BLOCKSETUP
static void rk3506_blocksetup(struct sdio_dev_s *dev,
              unsigned int blocklen, unsigned int nblocks);
#endif
static int  rk3506_recvsetup(struct sdio_dev_s *dev, uint8_t *buffer,
              size_t nbytes);
static int  rk3506_sendsetup(struct sdio_dev_s *dev,
              const uint8_t *buffer, size_t nbytes);
static int  rk3506_cancel(struct sdio_dev_s *dev);

static int  rk3506_waitresponse(struct sdio_dev_s *dev, uint32_t cmd);
static int  rk3506_recvshortcrc(struct sdio_dev_s *dev, uint32_t cmd,
              uint32_t *rshort);
static int  rk3506_recvlong(struct sdio_dev_s *dev, uint32_t cmd,
              uint32_t rlong[4]);
static int  rk3506_recvshort(struct sdio_dev_s *dev, uint32_t cmd,
              uint32_t *rshort);
static int  rk3506_recvnotimpl(struct sdio_dev_s *dev, uint32_t cmd,
              uint32_t *rnotimpl);

/* EVENT handler */

static void rk3506_waitenable(struct sdio_dev_s *dev,
              sdio_eventset_t eventset, uint32_t timeout);
static sdio_eventset_t rk3506_eventwait(struct sdio_dev_s *dev);
static void rk3506_callbackenable(struct sdio_dev_s *dev,
              sdio_eventset_t eventset);
static void rk3506_callback(struct rk3506_dev_s *priv);
static int  rk3506_registercallback(struct sdio_dev_s *dev,
              worker_t callback, void *arg);

#ifdef CONFIG_RK3506_SDMMC_DMA
/* DMA */

static void rk3506_dma_stop(struct rk3506_dev_s *priv);
static int  rk3506_dmasetup(struct rk3506_dev_s *priv,
              uint8_t *buffer, size_t buflen, bool write);
static int  rk3506_dmarecvsetup(struct sdio_dev_s *dev,
              uint8_t *buffer, size_t buflen);
static int  rk3506_dmasendsetup(struct sdio_dev_s *dev,
              const uint8_t *buffer, size_t buflen);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

struct rk3506_dev_s g_scard_dev =
{
  .dev =
  {
#ifdef CONFIG_SDIO_MUXBUS
    .lock             = rk3506_lock,
#endif
    .reset            = rk3506_reset,
    .capabilities     = rk3506_capabilities,
    .status           = rk3506_status,
    .widebus          = rk3506_widebus,
    .clock            = rk3506_clock,
    .attach           = rk3506_attach,
    .sendcmd          = rk3506_sendcmd,
#ifdef CONFIG_SDIO_BLOCKSETUP
    .blocksetup       = rk3506_blocksetup,
#endif
    .recvsetup        = rk3506_recvsetup,
    .sendsetup        = rk3506_sendsetup,
    .cancel           = rk3506_cancel,
    .waitresponse     = rk3506_waitresponse,
    .recv_r1          = rk3506_recvshortcrc,
    .recv_r2          = rk3506_recvlong,
    .recv_r3          = rk3506_recvshort,
    .recv_r4          = rk3506_recvnotimpl,
    .recv_r5          = rk3506_recvnotimpl,
    .recv_r6          = rk3506_recvshortcrc,
    .recv_r7          = rk3506_recvshort,
    .waitenable       = rk3506_waitenable,
    .eventwait        = rk3506_eventwait,
    .callbackenable   = rk3506_callbackenable,
    .registercallback = rk3506_registercallback,
#ifdef CONFIG_RK3506_SDMMC_DMA
    .dmarecvsetup     = rk3506_dmarecvsetup,
    .dmasendsetup     = rk3506_dmasendsetup,
#endif
  },
  .waitsem = SEM_INITIALIZER(0),
};

#ifdef CONFIG_RK3506_SDMMC_DMA
static struct sdmmc_dma_s g_sdmmc_dmadd[NUM_DMA_DESCRIPTORS]
  aligned_data(RK3506_DCACHE_LINESIZE);
static uint8_t g_sdmmc_dmabounce[RK3506_IDMAC_MAXSIZE]
  aligned_data(RK3506_DCACHE_LINESIZE);
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rk3506_getreg
 *
 * Description:
 *   This function may to used to intercept an monitor all register accesses.
 *   Clearly this is nothing you would want to do unless you are debugging
 *   this driver.
 *
 * Input Parameters:
 *   addr - The register address to read
 *
 * Returned Value:
 *   The value read from the register
 *
 ****************************************************************************/

#ifdef CONFIG_RK3506_SDMMC_REGDEBUG
static uint32_t rk3506_getreg(uint32_t addr)
{
  static uint32_t prevaddr = 0;
  static uint32_t preval   = 0;
  static uint32_t count    = 0;

  /* Read the value from the register */

  uint32_t val = getreg32(addr);

  /* Is this the same value that we read from the same register last time?
   * Are we polling the register?  If so, suppress some of the output.
   */

  if (addr == prevaddr && val == preval)
    {
      if (count == 0xffffffff || ++count > 3)
        {
          if (count == 4)
            {
              mcinfo("...\n");
            }

          return val;
        }
    }

  /* No this is a new address or value */

  else
    {
      /* Did we print "..." for the previous value? */

      if (count > 3)
        {
          /* Yes.. then show how many times the value repeated */

          mcinfo("[repeats %d more times]\n", count - 3);
        }

      /* Save the new address, value, and count */

      prevaddr = addr;
      preval   = val;
      count    = 1;
    }

  /* Show the register value read */

  mcinfo("%08x->%08x\n", addr, val);
  return val;
}
#endif

/****************************************************************************
 * Name: rk3506_putreg
 *
 * Description:
 *   This function may to used to intercept an monitor all register accesses.
 *   Clearly this is nothing you would want to do unless you are debugging
 *   this driver.
 *
 * Input Parameters:
 *   val - The value to write to the register
 *   addr - The register address to read
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

#ifdef CONFIG_RK3506_SDMMC_REGDEBUG
static void rk3506_putreg(uint32_t val, uint32_t addr)
{
  /* Show the register value being written */

  mcinfo("%08x<-%08x\n", addr, val);

  /* Write the value */

  putreg32(val, addr);
}
#endif

/****************************************************************************
 * Name: rk3506_setclock
 *
 * Description:
 *   Define the new clock frequency
 *
 * Input Parameters:
 *   clkdiv - A new division value to generate the needed frequency.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static inline void rk3506_setclock(uint32_t clkdiv)
{
  mcinfo("clkdiv=%08lx\n", (unsigned long)clkdiv);

  /* Disable the clock before setting frequency */

  rk3506_sdcard_clock(false);

  /* Inform CIU */

  rk3506_ciu_sendcmd(SDMMC_CMD_UPDCLOCK | SDMMC_CMD_WAITPREV, 0);

  /* Set Divider0 to desired value */

  rk3506_putreg(clkdiv, RK3506_SDMMC_CLKDIV);

  /* Inform CIU */

  rk3506_ciu_sendcmd(SDMMC_CMD_UPDCLOCK | SDMMC_CMD_WAITPREV, 0);

  /* Enable the clock */

  rk3506_sdcard_clock(true);

  /* Inform CIU */

  rk3506_ciu_sendcmd(SDMMC_CMD_UPDCLOCK | SDMMC_CMD_WAITPREV, 0);
}

/****************************************************************************
 * Name: rk3506_sdcard_clock
 *
 * Description: Enable/Disable the SDCard clock
 *
 * Input Parameters:
 *   enable - False = clock disabled; True = clock enabled.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static inline void rk3506_sdcard_clock(bool enable)
{
  if (enable)
    {
      rk3506_putreg(SDMMC_CLKENA_ENABLE, RK3506_SDMMC_CLKENA);
    }
  else
    {
      rk3506_putreg(0, RK3506_SDMMC_CLKENA);
    }
}

/****************************************************************************
 * Name: rk3506_ciu_sendcmd
 *
 * Description:
 *   Function to send command to Card interface unit (CIU)
 *
 * Input Parameters:
 *   cmd - The command to be executed
 *   arg - The argument to use with the command.
 *
 * Returned Value:
 *   Returns zero on success.  One will be returned on a timeout.
 *
 ****************************************************************************/

static int rk3506_ciu_sendcmd(uint32_t cmd, uint32_t arg)
{
  clock_t watchtime;

  mcinfo("cmd=%04lx arg=%04lx\n", (unsigned long)cmd, (unsigned long)arg);
  DEBUGASSERT((rk3506_getreg(RK3506_SDMMC_CMD) & SDMMC_CMD_STARTCMD) == 0);

  /* Set command arg reg */

  rk3506_putreg(arg, RK3506_SDMMC_CMDARG);
  rk3506_putreg(SDMMC_CMD_STARTCMD | cmd, RK3506_SDMMC_CMD);

  /* Poll until command is accepted by the CIU, or we timeout */

  watchtime = clock_systime_ticks();

  while ((rk3506_getreg(RK3506_SDMMC_CMD) & SDMMC_CMD_STARTCMD) != 0)
    {
      if (clock_systime_ticks() - watchtime > SDCARD_CMDTIMEOUT)
        {
          syslog(LOG_ERR,
                 "SDMMC: CIU command start timeout cmd=%08lx "
                 "status=%08lx rintsts=%08lx\n",
                 (unsigned long)rk3506_getreg(RK3506_SDMMC_CMD),
                 (unsigned long)rk3506_getreg(RK3506_SDMMC_STATUS),
                 (unsigned long)rk3506_getreg(RK3506_SDMMC_RINTSTS));
          mcerr("TMO Timed out (%08lX)\n",
                (unsigned long)rk3506_getreg(RK3506_SDMMC_CMD));
          return 1;
        }
    }

  return 0;
}

/****************************************************************************
 * Name: rk3506_wait_clear
 *
 * Description:
 *   Wait for controller reset bits to self-clear.  This helper uses a
 *   bounded microsecond delay rather than scheduler ticks because reset can
 *   run with interrupts disabled during early board initialization.
 *
 ****************************************************************************/

static bool rk3506_wait_clear(uint32_t addr, uint32_t mask,
                              const char *name)
{
  unsigned int timeout;
  uint32_t regval;

  for (timeout = 0; timeout < RK3506_RESET_TIMEOUT_US; timeout++)
    {
      regval = rk3506_getreg(addr);
      if ((regval & mask) == 0)
        {
          return true;
        }

      up_udelay(1);
    }

  syslog(LOG_ERR, "SDMMC: %s timeout reg=%08lx mask=%08lx\n",
         name, (unsigned long)rk3506_getreg(addr), (unsigned long)mask);
  mcerr("ERROR: SDMMC %s timeout, reg=%08lx mask=%08lx\n",
        name, (unsigned long)rk3506_getreg(addr), (unsigned long)mask);
  return false;
}

/****************************************************************************
 * Name: rk3506_enable_ints
 *
 * Description:
 *   Enable/disable SD card interrupts per functional settings.
 *
 * Input Parameters:
 *   priv - A reference to the SD card device state structure
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void rk3506_enable_ints(struct rk3506_dev_s *priv)
{
  uint32_t regval;

#ifdef CONFIG_RK3506_SDMMC_DMA
  mcinfo("waitmask=%04lx xfrmask=%04lx dmamask=%04lx RINTSTS=%08lx\n",
         (unsigned long)priv->waitmask, (unsigned long)priv->xfrmask,
         (unsigned long)priv->dmamask,
         (unsigned long)rk3506_getreg(RK3506_SDMMC_RINTSTS));

  /* Enable DMA-related interrupts */

  rk3506_putreg(priv->dmamask, RK3506_SDMMC_IDINTEN);

#else
  mcinfo("waitmask=%04lx xfrmask=%04lx RINTSTS=%08lx\n",
         (unsigned long)priv->waitmask, (unsigned long)priv->xfrmask,
         (unsigned long)rk3506_getreg(RK3506_SDMMC_RINTSTS));
#endif

  /* Enable SDMMC interrupts */

  regval = priv->xfrmask | priv->waitmask | SDCARD_INT_CDET;
  rk3506_putreg(regval, RK3506_SDMMC_INTMASK);
}

/****************************************************************************
 * Name: rk3506_disable_allints
 *
 * Description:
 *   Disable all SD card interrupts.
 *
 * Input Parameters:
 *   priv - A reference to the SD card device state structure
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void rk3506_disable_allints(struct rk3506_dev_s *priv)
{
#ifdef CONFIG_RK3506_SDMMC_DMA
  /* Disable DMA-related interrupts */

  rk3506_putreg(0, RK3506_SDMMC_IDINTEN);
  priv->dmamask = 0;
#endif

  /* Disable all SDMMC interrupts (except card detect) */

  rk3506_putreg(SDCARD_INT_CDET, RK3506_SDMMC_INTMASK);
  priv->waitmask = 0;
  priv->xfrmask  = 0;
}

/****************************************************************************
 * Name: rk3506_config_waitints
 *
 * Description:
 *   Enable/disable SD card interrupts needed to support the wait function
 *
 * Input Parameters:
 *   priv       - A reference to the SD card device state structure
 *   waitmask   - The set of bits in the SD card INTMASK register to set
 *   waitevents - Waited for events
 *   wkupevent  - Wake-up events
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void rk3506_config_waitints(struct rk3506_dev_s *priv,
                                  uint32_t waitmask,
                                  sdio_eventset_t waitevents,
                                  sdio_eventset_t wkupevent)
{
  irqstate_t flags;

  mcinfo("waitevents=%04x wkupevent=%04x\n",
         (unsigned)waitevents, (unsigned)wkupevent);

  /* Save all of the data and set the new interrupt mask in one, atomic
   * operation.
   */

  flags            = enter_critical_section();
  priv->waitevents = waitevents;
  priv->wkupevent  = wkupevent;
  priv->waitmask   = waitmask;

  rk3506_enable_ints(priv);
  leave_critical_section(flags);
}

/****************************************************************************
 * Name: rk3506_config_xfrints
 *
 * Description:
 *   Enable SD card interrupts needed to support the data transfer event
 *
 * Input Parameters:
 *   priv    - A reference to the SD card device state structure
 *   xfrmask - The set of bits in the SD card MASK register to set
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void rk3506_config_xfrints(struct rk3506_dev_s *priv, uint32_t xfrmask)
{
  irqstate_t flags;
  flags = enter_critical_section();

  priv->xfrmask = xfrmask;
  rk3506_enable_ints(priv);

  leave_critical_section(flags);
}

/****************************************************************************
 * Name: rk3506_config_dmaints
 *
 * Description:
 *   Enable DMA transfer interrupts
 *
 * Input Parameters:
 *   priv    - A reference to the SD card device state structure
 *   dmamask - The set of bits in the SD card MASK register to set
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

#ifdef CONFIG_RK3506_SDMMC_DMA
static void rk3506_config_dmaints(struct rk3506_dev_s *priv, uint32_t xfrmask,
                                 uint32_t dmamask)
{
  irqstate_t flags;
  flags = enter_critical_section();

  priv->xfrmask = xfrmask;
  priv->dmamask = dmamask;
  rk3506_enable_ints(priv);

  leave_critical_section(flags);
}
#endif

/****************************************************************************
 * Name: rk3506_eventtimeout
 *
 * Description:
 *   The watchdog timeout setup when the event wait start has expired without
 *   any other waited-for event occurring.
 *
 * Input Parameters:
 *   arg    - The argument
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   Always called from the interrupt level with interrupts disabled.
 *
 ****************************************************************************/

static void rk3506_eventtimeout(wdparm_t arg)
{
  struct rk3506_dev_s *priv = (struct rk3506_dev_s *)arg;

  mcinfo("arg=%08lx\n", (unsigned long)arg);

  /* There is always race conditions with timer expirations. */

  DEBUGASSERT((priv->waitevents & SDIOWAIT_TIMEOUT) != 0 ||
              priv->wkupevent != 0);

  /* Is a data transfer complete event expected? */

  if ((priv->waitevents & SDIOWAIT_TIMEOUT) != 0)
    {
      /* Terminate the complete data path, not just the waiter.  In
       * particular this stops IDMAC and prevents a timed-out transfer from
       * leaking into the next filesystem request.
       */

      syslog(LOG_ERR,
             "SDMMC: transfer timeout dma=%u write=%u remaining=%lu "
             "status=%08lx rintsts=%08lx idsts=%08lx\n",
#ifdef CONFIG_RK3506_SDMMC_DMA
             priv->dmamode,
#else
             0,
#endif
             priv->wrdir, (unsigned long)priv->remaining,
             (unsigned long)rk3506_getreg(RK3506_SDMMC_STATUS),
             (unsigned long)rk3506_getreg(RK3506_SDMMC_RINTSTS),
             (unsigned long)rk3506_getreg(RK3506_SDMMC_IDSTS));
      rk3506_endtransfer(priv,
                        SDIOWAIT_TRANSFERDONE | SDIOWAIT_TIMEOUT);
      mcerr("ERROR: Timeout: remaining: %d\n", priv->remaining);
    }
}

/****************************************************************************
 * Name: rk3506_endwait
 *
 * Description:
 *   Wake up a waiting thread if the waited-for event has occurred.
 *
 * Input Parameters:
 *   priv      - An instance of the SD card device interface
 *   wkupevent - The event that caused the wait to end
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   Always called from the interrupt level with interrupts disabled.
 *
 ****************************************************************************/

static void rk3506_endwait(struct rk3506_dev_s *priv,
                          sdio_eventset_t wkupevent)
{
  mcinfo("wkupevent=%04x\n", (unsigned)wkupevent);

  /* Cancel the watchdog timeout */

  wd_cancel(&priv->waitwdog);

  /* Disable event-related interrupts */

  rk3506_config_waitints(priv, 0, 0, wkupevent);

  /* Wake up the waiting thread */

  nxsem_post(&priv->waitsem);
}

/****************************************************************************
 * Name: rk3506_endtransfer
 *
 * Description:
 *   Terminate a transfer with the provided status.  This function is called
 *   only from the SD card interrupt handler when end-of-transfer conditions
 *   are detected.
 *
 * Input Parameters:
 *   priv   - An instance of the SD card device interface
 *   wkupevent - The event that caused the transfer to end
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   Always called from the interrupt level with interrupts disabled.
 *
 ****************************************************************************/

static void rk3506_endtransfer(struct rk3506_dev_s *priv,
                              sdio_eventset_t wkupevent)
{
  mcinfo("wkupevent=%04x\n", (unsigned)wkupevent);

  /* Disable all transfer related interrupts */

#ifdef CONFIG_RK3506_SDMMC_DMA
  if (priv->dmamode)
    {
      bool success;

      success = (wkupevent & (SDIOWAIT_ERROR | SDIOWAIT_TIMEOUT)) == 0;
      rk3506_config_dmaints(priv, 0, 0);
      rk3506_dma_stop(priv);
      priv->dmafastready = success;
    }
  else
#endif
    {
      rk3506_config_xfrints(priv, 0);
    }

  /* Clearing pending interrupt status on all transfer related interrupts */

  rk3506_putreg(SDCARD_TRANSFER_ALL, RK3506_SDMMC_RINTSTS);

  /* Mark the transfer finished */

  priv->remaining = 0;

  /* Is a thread wait for these data transfer complete events? */

  if ((priv->waitevents & wkupevent) != 0)
    {
      /* Yes.. wake up any waiting threads */

      rk3506_endwait(priv, wkupevent);
    }
}

/****************************************************************************
 * Name: rk3506_sdmmc_interrupt
 *
 * Description:
 *   SD card interrupt handler
 *
 * Input Parameters:
 *   dev - An instance of the SD card device interface
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static int rk3506_sdmmc_interrupt(int irq, void *context, void *arg)
{
  struct rk3506_dev_s *priv = &g_scard_dev;
  uint32_t enabled;
  uint32_t pending;

  /* Loop while there are pending interrupts.  Check the SD card status
   * register.  Mask out all bits that don't correspond to enabled
   * interrupts.  (This depends on the fact that bits are ordered
   * the same in both the STA and MASK register).  If there are non-zero
   * bits remaining, then we have work to do here.
   */

  while ((enabled = rk3506_getreg(RK3506_SDMMC_MINTSTS)) != 0)
    {
      /* Clear pending status */

      rk3506_putreg(enabled, RK3506_SDMMC_RINTSTS);

#ifdef CONFIG_MMCSD_HAVE_CARDDETECT
      /* Handle in card detection events ************************************/

      if ((enabled & SDMMC_INT_CDET) != 0)
        {
          sdio_statset_t cdstatus;

          /* Update card status */

          cdstatus = priv->cdstatus;
          if ((rk3506_getreg(RK3506_SDMMC_CDETECT) &
              SDMMC_CDETECT_NOTPRESENT) == 0)
            {
              priv->cdstatus |= SDIO_STATUS_PRESENT;

#ifdef CONFIG_MMCSD_HAVE_WRITEPROTECT
              if ((rk3506_getreg(RK3506_SDMMC_WRTPRT) &
                  SDMMC_WRTPRT_PROTECTED) != 0)
                {
                  priv->cdstatus |= SDIO_STATUS_WRPROTECTED;
                }
              else
#endif
                {
                  priv->cdstatus &= ~SDIO_STATUS_WRPROTECTED;
                }

#ifdef CONFIG_RK3506_SDMMC_PWRCTRL
              /* Enable/ power to the SD card */

              rk3506_putreg(SDMMC_PWREN, RK3506_SDMMC_PWREN);
#endif
            }
          else
            {
              priv->cdstatus &=
                ~(SDIO_STATUS_PRESENT | SDIO_STATUS_WRPROTECTED);

#ifdef CONFIG_RK3506_SDMMC_PWRCTRL
              /* Disable power to the SD card */

              rk3506_putreg(0, RK3506_SDMMC_PWREN);
#endif
            }

          mcinfo("cdstatus OLD: %02x NEW: %02x\n", cdstatus, priv->cdstatus);

          /* Perform any requested callback if the status has changed */

          if (cdstatus != priv->cdstatus)
            {
              rk3506_callback(priv);
            }
        }
#endif

      /* Handle data transfer events ****************************************/

      pending = enabled & priv->xfrmask;
      if (pending != 0)
        {
          /* Handle data request events */

#ifdef CONFIG_RK3506_SDMMC_DMA
          if (!priv->dmamode)
#endif
            {
              if ((pending & SDMMC_INT_TXDR) != 0)
                {
                  uint32_t status;

                  /* Transfer data to the TX FIFO */

                  DEBUGASSERT(priv->wrdir);

                  for (status = rk3506_getreg(RK3506_SDMMC_STATUS);
                       (status & SDMMC_STATUS_FIFOFULL) == 0 &&
                       priv->remaining > 0;
                       status = rk3506_getreg(RK3506_SDMMC_STATUS))
                    {
                      rk3506_putreg(*priv->buffer, RK3506_SDMMC_DATA);
                      priv->buffer++;
                      priv->remaining -= 4;
                    }
                }
              else if (!priv->wrdir &&
                       (pending & (SDMMC_INT_RXDR | SDMMC_INT_DTO)) != 0)
                {
                  uint32_t status;

                  /* Transfer data from the RX FIFO */

                  DEBUGASSERT(!priv->wrdir);

                  for (status = rk3506_getreg(RK3506_SDMMC_STATUS);
                       (status & SDMMC_STATUS_FIFOEMPTY) == 0 &&
                       priv->remaining > 0;
                       status = rk3506_getreg(RK3506_SDMMC_STATUS))
                    {
                      *priv->buffer = rk3506_getreg(RK3506_SDMMC_DATA);
                      priv->buffer++;
                      priv->remaining -= 4;
                    }
                }
            }

          /* Check for transfer errors */

          /* Handle data block send/receive CRC failure */

          if ((pending & SDMMC_INT_DCRC) != 0)
            {
              /* Terminate the transfer with an error */

              syslog(LOG_ERR,
                     "SDMMC: data CRC error pending=%08lx remaining=%ld\n",
                     (unsigned long)pending, (long)priv->remaining);
              mcerr("ERROR: Data CRC failure, pending=%08lx remaining: %ld\n",
                    (unsigned long)pending, (long)priv->remaining);

              rk3506_endtransfer(priv,
                                SDIOWAIT_TRANSFERDONE | SDIOWAIT_ERROR);
            }

          /* Handle data timeout error */

          else if ((pending & SDMMC_INT_DRTO) != 0)
            {
              /* Terminate the transfer with an error */

              syslog(LOG_ERR,
                     "SDMMC: data timeout pending=%08lx remaining=%ld\n",
                     (unsigned long)pending, (long)priv->remaining);
              mcerr("ERROR: Data timeout, pending=%08lx remaining: %ld\n",
                    (unsigned long)pending, (long)priv->remaining);

              rk3506_endtransfer(priv,
                                SDIOWAIT_TRANSFERDONE | SDIOWAIT_TIMEOUT);
            }

          /* Handle FIFO overrun/underrun error */

          else if ((pending & SDMMC_INT_FRUN) != 0)
            {
              /* Terminate the transfer with an error */

              syslog(LOG_ERR,
                     "SDMMC: FIFO error pending=%08lx remaining=%ld\n",
                     (unsigned long)pending, (long)priv->remaining);
              mcerr("ERROR: FIFO overrun/underrun, pending=%08lx "
                    "remaining: %ld\n",
                    (unsigned long)pending, (long)priv->remaining);

              rk3506_endtransfer(priv,
                                SDIOWAIT_TRANSFERDONE | SDIOWAIT_ERROR);
            }

          /* Handle start bit error */

          else if ((pending & SDMMC_INT_SBE) != 0)
            {
              /* Terminate the transfer with an error */

              syslog(LOG_ERR,
                     "SDMMC: start-bit error pending=%08lx remaining=%ld\n",
                     (unsigned long)pending, (long)priv->remaining);
              mcerr("ERROR: Start bit, pending=%08lx remaining: %ld\n",
                    (unsigned long)pending, (long)priv->remaining);

              rk3506_endtransfer(priv,
                                SDIOWAIT_TRANSFERDONE | SDIOWAIT_ERROR);
            }

          /* Handle data end events.  Note that RXDR may accompany DTO, DTO
           * will be set on received while there is still data in the FIFO.
           * So for the case of receiving, we don't actually even enable the
           * DTO interrupt.
           */

          else if ((pending & SDMMC_INT_DTO) != 0)
            {
#ifdef CONFIG_RK3506_SDMMC_DMA
              if (priv->dmamode)
                {
                  uint32_t idsts;

                  idsts = rk3506_getreg(RK3506_SDMMC_IDSTS);
                  rk3506_putreg(idsts, RK3506_SDMMC_IDSTS);

                  if ((idsts & (SDMMC_IDSTS_FBE | SDMMC_IDSTS_DU |
                                SDMMC_IDSTS_CES | SDMMC_IDSTS_AIS |
                                SDMMC_IDSTS_EB_MASK)) != 0)
                    {
                      syslog(LOG_ERR,
                             "SDMMC: IDMAC completion error idsts=%08lx "
                             "dsc=%08lx buf=%08lx\n",
                             (unsigned long)idsts,
                             (unsigned long)rk3506_getreg(
                               RK3506_SDMMC_DSCADDR),
                             (unsigned long)rk3506_getreg(
                               RK3506_SDMMC_BUFADDR));
                      rk3506_endtransfer(priv,
                                        SDIOWAIT_TRANSFERDONE |
                                        SDIOWAIT_ERROR);
                      continue;
                    }

                  if (!priv->wrdir)
                    {
                      up_invalidate_dcache((uintptr_t)priv->dmabuffer,
                                           (uintptr_t)priv->dmabuffer +
                                           priv->dmalen);

                      if (priv->dmabounce)
                        {
                          memcpy(priv->buffer, priv->dmabuffer,
                                 priv->dmalen);
                        }
                    }

                  priv->remaining = 0;
                  rk3506_endtransfer(priv, SDIOWAIT_TRANSFERDONE);
                  continue;
                }
#endif

              if (priv->remaining == 0)
                {
                  rk3506_endtransfer(priv, SDIOWAIT_TRANSFERDONE);
                }
              else
                {
                  syslog(LOG_ERR,
                         "SDMMC: short transfer remaining=%ld\n",
                         (long)priv->remaining);
                  mcerr("ERROR: Short transfer, remaining: %ld\n",
                        (long)priv->remaining);
                  rk3506_endtransfer(priv,
                                    SDIOWAIT_TRANSFERDONE | SDIOWAIT_ERROR);
                }
            }
        }

      /* Handle wait events *************************************************/

      pending = enabled & priv->waitmask;
      if (pending != 0)
        {
          /* Is this a response error event? */

          if ((pending & SDCARD_INT_RESPERR) != 0)
            {
              /* If response errors are enabled, then we must certainly be
               * waiting for a response.
               */

              DEBUGASSERT((priv->waitevents & SDIOWAIT_RESPONSEDONE) != 0);

              /* Wake the thread up */

              mcerr("ERROR: Response error, pending=%08lx\n",
                    (unsigned long)pending);
              rk3506_endwait(priv, SDIOWAIT_RESPONSEDONE | SDIOWAIT_ERROR);
            }

          /* Is this a command (plus response) completion event? */

          else if ((pending & SDMMC_INT_CDONE) != 0)
            {
              /* Yes.. Is their a thread waiting for response done? */

              if ((priv->waitevents & SDIOWAIT_RESPONSEDONE) != 0)
                {
                  /* Yes.. wake the thread up */

                  rk3506_endwait(priv, SDIOWAIT_RESPONSEDONE);
                }

              /* NO.. Is their a thread waiting for command done? */

              else if ((priv->waitevents & SDIOWAIT_CMDDONE) != 0)
                {
                  /* Yes.. wake the thread up */

                  rk3506_endwait(priv, SDIOWAIT_CMDDONE);
                }
            }
        }
    }

#ifdef CONFIG_RK3506_SDMMC_DMA
  /* DMA error events *******************************************************/

  pending = rk3506_getreg(RK3506_SDMMC_IDSTS);
  if (priv->dmamode && (pending & priv->dmamask) != 0)
    {
      syslog(LOG_ERR,
             "SDMMC: IDMAC error idsts=%08lx dsc=%08lx buf=%08lx\n",
             (unsigned long)pending,
             (unsigned long)rk3506_getreg(RK3506_SDMMC_DSCADDR),
             (unsigned long)rk3506_getreg(RK3506_SDMMC_BUFADDR));
      mcerr("ERROR: IDSTS=%08lx\n", (unsigned long)pending);

      /* Clear the pending interrupts */

      rk3506_putreg(pending, RK3506_SDMMC_IDSTS);

      /* Abort the transfer */

      rk3506_endtransfer(priv, SDIOWAIT_TRANSFERDONE | SDIOWAIT_ERROR);
    }
#endif

  return OK;
}

/****************************************************************************
 * Name: rk3506_lock
 *
 * Description:
 *   Locks the bus. Function calls low-level multiplexed bus routines to
 *   resolve bus requests and acknowledgment issues.
 *
 * Input Parameters:
 *   dev    - An instance of the SD card device interface
 *   lock   - TRUE to lock, FALSE to unlock.
 *
 * Returned Value:
 *   OK on success; a negated errno on failure
 *
 ****************************************************************************/

#ifdef CONFIG_SDIO_MUXBUS
static int rk3506_lock(struct sdio_dev_s *dev, bool lock)
{
  /* Single SD card instance so there is only one possibility.  The multiplex
   * bus is part of board support package.
   */

  /* FIXME: Implement the below function to support bus share:
   *
   * rk3506_muxbus_sdio_lock(lock);
   */

  return OK;
}
#endif

/****************************************************************************
 * Name: rk3506_reset
 *
 * Description:
 *   Reset the SD card controller.  Undo all setup and initialization.
 *
 * Input Parameters:
 *   dev    - An instance of the SD card device interface
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void rk3506_reset(struct sdio_dev_s *dev)
{
  struct rk3506_dev_s *priv = (struct rk3506_dev_s *)dev;
  irqstate_t flags;
  uint32_t regval;

  mcinfo("Resetting...\n");

  flags = enter_critical_section();
  priv->resetok = false;

  /* Reset DMA controller internal registers. */

  rk3506_putreg(SDMMC_BMOD_SWR, RK3506_SDMMC_BMOD);
  if (!rk3506_wait_clear(RK3506_SDMMC_BMOD, SDMMC_BMOD_SWR,
                         "BMOD reset"))
    {
      leave_critical_section(flags);
      return;
    }

  /* Reset all blocks */

  rk3506_putreg(SDMMC_CTRL_CNTLRRESET | SDMMC_CTRL_FIFORESET |
               SDMMC_CTRL_DMARESET, RK3506_SDMMC_CTRL);

  if (!rk3506_wait_clear(RK3506_SDMMC_CTRL,
                         SDMMC_CTRL_CNTLRRESET |
                         SDMMC_CTRL_FIFORESET |
                         SDMMC_CTRL_DMARESET,
                         "CTRL reset"))
    {
      leave_critical_section(flags);
      return;
    }

  /* Reset data */

  priv->waitevents = 0;      /* Set of events to be waited for */
  priv->waitmask   = 0;      /* Interrupt enables for event waiting */
  priv->wkupevent  = 0;      /* The event that caused the wakeup */

  wd_cancel(&priv->waitwdog); /* Cancel any timeouts */

  /* Interrupt mode data transfer support */

  priv->buffer     = 0;      /* Address of current R/W buffer */
  priv->remaining  = 0;      /* Number of bytes remaining in the transfer */
  priv->xfrmask    = 0;      /* Interrupt enables for data transfer */
#ifdef CONFIG_RK3506_SDMMC_DMA
  priv->dmamask    = 0;      /* Interrupt enables for DMA transfer */
  priv->dmamode    = false;
  priv->dmabounce  = false;
  priv->dmafastready = false;
  priv->dmabuffer  = NULL;
  priv->dmalen     = 0;
#endif

  /* DMA data transfer support */

  priv->widebus    = true;   /* Required for DMA support */
  /* This first storage stage is intentionally fixed-media and read-only.
   * The card must be present because U-Boot loaded openvela from it.
   */

  priv->cdstatus   = SDIO_STATUS_PRESENT;
#ifdef CONFIG_MMCSD_READONLY
  priv->cdstatus  |= SDIO_STATUS_WRPROTECTED;
#endif

  /* Select 1-bit wide bus */

  rk3506_putreg(SDMMC_CTYPE_WIDTH1, RK3506_SDMMC_CTYPE);

  /* Enable interrupts */

  regval  = rk3506_getreg(RK3506_SDMMC_CTRL);
  regval |= SDMMC_CTRL_INTENABLE;
  rk3506_putreg(regval, RK3506_SDMMC_CTRL);

  /* Disable Interrupts except for card detection. */

  rk3506_putreg(SDCARD_INT_CDET, RK3506_SDMMC_INTMASK);

#ifdef CONFIG_MMCSD_HAVE_CARDDETECT
  /* Set the card debounce time.  Number of host clocks (SD_CLK) used by
   * debounce filter logic for card detect.  typical debounce time is 5-25
   * ms.
   */

  rk3506_putreg(DEBOUNCE_TICKS, RK3506_SDMMC_DEBNCE);
#endif

  /* Clear to Interrupts */

  rk3506_putreg(0xffffffff, RK3506_SDMMC_RINTSTS);

  /* Define MAX Timeout */

  rk3506_putreg(0x7fffffff, RK3506_SDMMC_TMOUT);

  /* Disable clock to CIU (needs latch) */

  rk3506_putreg(0, RK3506_SDMMC_CLKENA);
  priv->resetok = true;
  leave_critical_section(flags);

#if defined(CONFIG_RK3506_SDMMC_PWRCTRL) && !defined(CONFIG_MMCSD_HAVE_CARDDETECT)
  /* Enable power to the SD card */

  rk3506_putreg(SDMMC_PWREN, RK3506_SDMMC_PWREN);
#endif
}

/****************************************************************************
 * Name: rk3506_capabilities
 *
 * Description:
 *   Get capabilities (and limitations) of the SDIO driver (optional)
 *
 * Input Parameters:
 *   dev   - Device-specific state data
 *
 * Returned Value:
 *   Returns a bitset of status values (see SDIO_CAPS_* defines)
 *
 ****************************************************************************/

static sdio_capset_t rk3506_capabilities(struct sdio_dev_s *dev)
{
  sdio_capset_t caps = 0;

  caps |= SDIO_CAPS_DMABEFOREWRITE;

#ifdef CONFIG_SDIO_WIDTH_D1_ONLY
  caps |= SDIO_CAPS_1BIT_ONLY;
#else
  caps |= SDIO_CAPS_4BIT;
#endif
#ifdef CONFIG_RK3506_SDMMC_DMA
  caps |= SDIO_CAPS_DMASUPPORTED;
#endif

  return caps;
}

/****************************************************************************
 * Name: rk3506_status
 *
 * Description:
 *   Get SD card status.
 *
 * Input Parameters:
 *   dev   - Device-specific state data
 *
 * Returned Value:
 *   Returns a bitset of status values (see rk3506_status_* defines)
 *
 ****************************************************************************/

static sdio_statset_t rk3506_status(struct sdio_dev_s *dev)
{
  struct rk3506_dev_s *priv = (struct rk3506_dev_s *)dev;

#ifdef CONFIG_MMCSD_HAVE_CARDDETECT
  if ((rk3506_getreg(RK3506_SDMMC_CDETECT) & SDMMC_CDETECT_NOTPRESENT) == 0)
    {
      priv->cdstatus |= SDIO_STATUS_PRESENT;
    }
  else
    {
      priv->cdstatus &= ~SDIO_STATUS_PRESENT;
    }
#endif

#ifdef CONFIG_MMCSD_READONLY
  priv->cdstatus |= SDIO_STATUS_WRPROTECTED;
#else
  priv->cdstatus &= ~SDIO_STATUS_WRPROTECTED;
#endif

  mcinfo("cdstatus=%02x\n", priv->cdstatus);

  return priv->cdstatus;
}

/****************************************************************************
 * Name: rk3506_widebus
 *
 * Description:
 *   Called after change in Bus width has been selected (via ACMD6).  Most
 *   controllers will need to perform some special operations to work
 *   correctly in the new bus mode.
 *
 * Input Parameters:
 *   dev  - An instance of the SD card device interface
 *   wide - true: wide bus (4-bit) bus mode enabled
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void rk3506_widebus(struct sdio_dev_s *dev, bool wide)
{
  struct rk3506_dev_s *priv = (struct rk3506_dev_s *)dev;

  mcinfo("wide=%d\n", wide);
  priv->widebus = wide;
  rk3506_putreg(wide ? SDMMC_CTYPE_WIDTH4 : SDMMC_CTYPE_WIDTH1,
                RK3506_SDMMC_CTYPE);
}

/****************************************************************************
 * Name: rk3506_clock
 *
 * Description:
 *   Enable/disable SD card clocking
 *
 * Input Parameters:
 *   dev  - An instance of the SD card device interface
 *   rate - Specifies the clocking to use (see enum sdio_clock_e)
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void rk3506_clock(struct sdio_dev_s *dev, enum sdio_clock_e rate)
{
  struct rk3506_dev_s *priv = (struct rk3506_dev_s *)dev;
  uint32_t actual;
  uint32_t divider;
  uint8_t clkdiv;
  uint8_t ctype;
  bool enabled = false;
  bool widebus = false;

  switch (rate)
    {
      /* Disable clocking (with default ID mode divisor) */

      default:
      case CLOCK_SDIO_DISABLED:
        clkdiv  = SDMMC_CLKDIV0(BOARD_CLKDIV_INIT);
        ctype   = SDCARD_BUS_D1;
        enabled = false;
        widebus = false;
        break;

      /* Enable in initial ID mode clocking (<400KHz) */

      case CLOCK_IDMODE:
        clkdiv  = SDMMC_CLKDIV0(BOARD_CLKDIV_INIT);
        ctype   = SDCARD_BUS_D1;
        enabled = true;
        widebus = false;
        break;

      /* Enable in MMC normal operation clocking */

      case CLOCK_MMC_TRANSFER:
        clkdiv  = SDMMC_CLKDIV0(BOARD_CLKDIV_MMCXFR);
        ctype   = SDCARD_BUS_D1;
        enabled = true;
        widebus = false;
        break;

      /* SD normal operation clocking (wide 4-bit mode) */

      case CLOCK_SD_TRANSFER_4BIT:
#ifndef CONFIG_SDIO_WIDTH_D1_ONLY
        clkdiv  = SDMMC_CLKDIV0(BOARD_CLKDIV_SDWIDEXFR);
        ctype   = SDCARD_BUS_D4;
        enabled = true;
        widebus = true;
        break;
#endif

      /* SD normal operation clocking (narrow 1-bit mode) */

      case CLOCK_SD_TRANSFER_1BIT:
        clkdiv  = SDMMC_CLKDIV0(BOARD_CLKDIV_SDXFR);
        ctype   = SDCARD_BUS_D1;
        enabled = true;
        widebus = false;
        break;
    }

  /* Setup the card bus width */

  mcinfo("widebus=%d\n", widebus);

  priv->widebus = widebus;
  rk3506_putreg(ctype, RK3506_SDMMC_CTYPE);

  /* Set the new clock frequency division */

  rk3506_setclock(clkdiv);

  /* Enable the new clock */

  rk3506_sdcard_clock(enabled);

  if (enabled)
    {
      divider = (clkdiv & SDMMC_CLKDIV0_MASK) >> SDMMC_CLKDIV0_SHIFT;
      actual = divider == 0 ? BOARD_SDMMC_FREQUENCY :
               BOARD_SDMMC_FREQUENCY / (2 * divider);

      syslog(LOG_INFO,
             "SDMMC: clock %lu Hz, bus %u-bit (divider=%lu)\n",
             (unsigned long)actual, widebus ? 4 : 1,
             (unsigned long)divider);
    }
}

/****************************************************************************
 * Name: rk3506_attach
 *
 * Description:
 *   Attach and prepare interrupts
 *
 * Input Parameters:
 *   dev - An instance of the SD card device interface
 *
 * Returned Value:
 *   OK on success; A negated errno on failure.
 *
 ****************************************************************************/

static int rk3506_attach(struct sdio_dev_s *dev)
{
  int ret;
  uint32_t regval;

  mcinfo("Attaching..\n");

  /* Attach the SD card interrupt handler */

  ret = irq_attach(RK3506_IRQ_SDMMC, rk3506_sdmmc_interrupt, NULL);
  if (ret == OK)
    {
      /* Disable all interrupts at the SD card controller and clear static
       * interrupt flags
       */

      rk3506_putreg(0, RK3506_SDMMC_INTMASK);
      rk3506_putreg(SDMMC_INT_ALL, RK3506_SDMMC_RINTSTS);

      /* Enable Interrupts to happen when the INTMASK is activated */

      regval  = rk3506_getreg(RK3506_SDMMC_CTRL);
      regval |= SDMMC_CTRL_INTENABLE;
      rk3506_putreg(regval, RK3506_SDMMC_CTRL);

      /* Enable card detection interrupts */

      rk3506_putreg(SDCARD_INT_CDET, RK3506_SDMMC_INTMASK);

      /* Enable SD card interrupts at the NVIC.  They can now be enabled at
       * the SD card controller as needed.
       */

      up_enable_irq(RK3506_IRQ_SDMMC);
    }

  return ret;
}

/****************************************************************************
 * Name: rk3506_sendcmd
 *
 * Description:
 *   Send the SD card command
 *
 * Input Parameters:
 *   dev  - An instance of the SD card device interface
 *   cmd  - The command to send (32-bits, encoded)
 *   arg  - 32-bit argument required with some commands
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static int rk3506_sendcmd(struct sdio_dev_s *dev, uint32_t cmd,
                         uint32_t arg)
{
  uint32_t regval = 0;
  uint32_t cmdidx;

  mcinfo("cmd=%04lx arg=%04lx\n",
         (unsigned long)cmd, (unsigned long)arg);

  cmdidx = (cmd & MMCSD_CMDIDX_MASK) >> MMCSD_CMDIDX_SHIFT;

#ifdef CONFIG_MMCSD_READONLY
  /* This port initially shares its SD card with the boot chain.  Reject
   * write-data commands in the controller lower half as an independent
   * barrier in addition to the write-protected media status.
   */

  if ((cmd & MMCSD_WRDATAXFR) == MMCSD_WRDATAXFR)
    {
      mcerr("ERROR: Write command rejected in read-only mode\n");
      return -EROFS;
    }
#endif

  /* CMD12 terminates an active multi-block transfer.  DesignWare MMC
   * requires STOP_ABORT in addition to the command index; both Rockchip
   * U-Boot and the mature NuttX DesignWare driver use this sequence.
   */

  if (cmdidx == MMCSD_CMDIDX12)
    {
      regval |= SDMMC_CMD_STOPABORT;
    }

  /* The CMD0 needs the SENDINIT CMD */

  if (cmd == 0)
    {
      regval |= SDMMC_CMD_SENDINIT;
    }

  /* Is this a Read/Write Transfer Command ? */

  if ((cmd & MMCSD_WRDATAXFR) == MMCSD_WRDATAXFR)
    {
      regval |= SDMMC_CMD_DATAXFREXPTD | SDMMC_CMD_WRITE |
                SDMMC_CMD_WAITPREV;
    }
  else if ((cmd & MMCSD_RDDATAXFR) == MMCSD_RDDATAXFR)
    {
      regval |= SDMMC_CMD_DATAXFREXPTD | SDMMC_CMD_WAITPREV;
    }

  /* Set WAITRESP bits */

  switch (cmd & MMCSD_RESPONSE_MASK)
    {
    case MMCSD_NO_RESPONSE:
      regval |= SDMMC_CMD_NORESPONSE;
      break;

    case MMCSD_R1B_RESPONSE:
      regval |= SDMMC_CMD_WAITPREV;
    case MMCSD_R1_RESPONSE:
    case MMCSD_R5_RESPONSE:
    case MMCSD_R6_RESPONSE:
    case MMCSD_R7_RESPONSE:
      regval |= SDMMC_CMD_SHORTRESPONSE | SDMMC_CMD_RESPCRC;
      break;

    case MMCSD_R3_RESPONSE:
    case MMCSD_R4_RESPONSE:
      regval |= SDMMC_CMD_SHORTRESPONSE;
      break;

    case MMCSD_R2_RESPONSE:
      regval |= SDMMC_CMD_LONGRESPONSE | SDMMC_CMD_RESPCRC;
      break;
    }

  /* Set the command index */

  regval |= cmdidx;

  /* Rockchip's HAL and U-Boot both select the CIU hold register for every
   * command sent to the card.  Without it, read commands happen to work on
   * RK3506, but the data driven after CMD24 can violate the card-side setup
   * window and is rejected with DCRC.
   */

  regval |= SDMMC_CMD_USEHOLD;

  mcinfo("cmd: %04lx arg: %04lx regval: %08lx\n",
         (unsigned long)cmd, (unsigned long)arg, (unsigned long)regval);

  /* Write the SD card CMD */

  rk3506_putreg(SDCARD_RESPDONE_CLEAR | SDCARD_CMDDONE_CLEAR,
               RK3506_SDMMC_RINTSTS);
  if (rk3506_ciu_sendcmd(regval, arg) != 0)
    {
      return -ETIMEDOUT;
    }

  return OK;
}

/****************************************************************************
 * Name: rk3506_blocksetup
 *
 * Description:
 *   Configure block size and the number of blocks for next transfer
 *
 * Input Parameters:
 *   dev       - An instance of the SDIO device interface
 *   blocklen  - The selected block size.
 *   nblocklen - The number of blocks to transfer
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

#ifdef CONFIG_SDIO_BLOCKSETUP
static void rk3506_blocksetup(struct sdio_dev_s *dev,
                             unsigned int blocklen, unsigned int nblocks)
{
  mcinfo("blocklen=%u, total transfer=%u (%u blocks)\n",
         blocklen, blocklen * nblocks, nblocks);

  /* Configure block size for next transfer */

  rk3506_putreg(blocklen, RK3506_SDMMC_BLKSIZ);
  rk3506_putreg(blocklen * nblocks, RK3506_SDMMC_BYTCNT);
}
#endif

/****************************************************************************
 * Name: rk3506_recvsetup
 *
 * Description:
 *   Setup hardware in preparation for data transfer from the card in non-
 *   DMA (interrupt driven mode).  This method will do whatever controller
 *   setup is necessary.  This would be called for SD memory just BEFORE
 *   sending CMD13 (SEND_STATUS), CMD17 (READ_SINGLE_BLOCK), CMD18
 *   (READ_MULTIPLE_BLOCKS), ACMD51 (SEND_SCR), etc.  Normally,
 *   SDCARD_WAITEVENT will be called to receive the indication that the
 *   transfer is complete.
 *
 * Input Parameters:
 *   dev    - An instance of the SD card device interface
 *   buffer - Address of the buffer in which to receive the data
 *   nbytes - The number of bytes in the transfer
 *
 * Returned Value:
 *   Number of bytes sent on success; a negated errno on failure
 *
 ****************************************************************************/

static int rk3506_recvsetup(struct sdio_dev_s *dev, uint8_t *buffer,
                           size_t nbytes)
{
  struct rk3506_dev_s *priv = (struct rk3506_dev_s *)dev;
  uint32_t regval;

  mcinfo("nbytes=%ld\n", (long) nbytes);

  DEBUGASSERT(priv != NULL && buffer != NULL && nbytes > 0);
  DEBUGASSERT(((uint32_t)buffer & 3) == 0);

  /* Match the Rockchip data-command sequence and discard any residual words
   * left by an aborted or failed transfer before accepting new data.
   */

  regval  = rk3506_getreg(RK3506_SDMMC_CTRL);
  regval |= SDMMC_CTRL_FIFORESET;
  rk3506_putreg(regval, RK3506_SDMMC_CTRL);

  if (!rk3506_wait_clear(RK3506_SDMMC_CTRL, SDMMC_CTRL_FIFORESET,
                         "read FIFO reset"))
    {
      return -ETIMEDOUT;
    }

  /* Save the destination buffer information for use by the interrupt
   * handler.
   */

  priv->buffer    = (uint32_t *)buffer;
  priv->remaining = nbytes;
  priv->wrdir     = false;
#ifdef CONFIG_RK3506_SDMMC_DMA
  priv->dmamode   = false;
  priv->dmafastready = false;
#endif

  /* Configure the FIFO so that we will receive the RXDR interrupt whenever
   * there are more than 1 words (at least 8 bytes) in the RX FIFO.
   */

  rk3506_putreg(SDMMC_FIFOTH_RXWMARK(1), RK3506_SDMMC_FIFOTH);

#ifdef CONFIG_RK3506_SDMMC_DMA
  /* Make sure that internal DMA is disabled */

  rk3506_putreg(0, RK3506_SDMMC_BMOD);

  regval  = rk3506_getreg(RK3506_SDMMC_CTRL);
  regval &= ~(SDMMC_CTRL_INTDMA | SDMMC_CTRL_DMAENABLE);
  rk3506_putreg(regval, RK3506_SDMMC_CTRL);
#endif

  /* Flush ints before we start */

  rk3506_putreg(SDCARD_TRANSFER_ALL, RK3506_SDMMC_RINTSTS);

  /* Configure the transfer interrupts */

  rk3506_config_xfrints(priv, SDCARD_RECV_MASK);
  return OK;
}

/****************************************************************************
 * Name: rk3506_sendsetup
 *
 * Description:
 *   Setup hardware in preparation for data transfer from the card.  This
 *   method will do whatever controller setup is necessary.  This would be
 *   called for SD memory just AFTER sending CMD24 (WRITE_BLOCK), CMD25
 *   (WRITE_MULTIPLE_BLOCK), ... and before SDCARD_SENDDATA is called.
 *
 * Input Parameters:
 *   dev    - An instance of the SD card device interface
 *   buffer - Address of the buffer containing the data to send
 *   nbytes - The number of bytes in the transfer
 *
 * Returned Value:
 *   Number of bytes sent on success; a negated errno on failure
 *
 ****************************************************************************/

static int rk3506_sendsetup(struct sdio_dev_s *dev,
                           const uint8_t *buffer, size_t nbytes)
{
  struct rk3506_dev_s *priv = (struct rk3506_dev_s *)dev;
  uint32_t regval;
  uint32_t status;

  mcinfo("nbytes=%ld\n", (long)nbytes);

  DEBUGASSERT(priv != NULL && buffer != NULL && nbytes > 0);
  DEBUGASSERT(((uint32_t)buffer & 3) == 0);

  /* Start every PIO write with a known-empty FIFO, just as the Rockchip
   * U-Boot driver does before issuing a data command.  A stale FIFO word
   * changes the on-wire CRC even though the requested byte count is right.
   */

  regval  = rk3506_getreg(RK3506_SDMMC_CTRL);
  regval |= SDMMC_CTRL_FIFORESET;
  rk3506_putreg(regval, RK3506_SDMMC_CTRL);

  if (!rk3506_wait_clear(RK3506_SDMMC_CTRL, SDMMC_CTRL_FIFORESET,
                         "write FIFO reset"))
    {
      return -ETIMEDOUT;
    }

  /* Save the source buffer information for use by the interrupt handler */

  priv->buffer    = (uint32_t *)buffer;
  priv->remaining = nbytes;
  priv->wrdir     = true;
#ifdef CONFIG_RK3506_SDMMC_DMA
  priv->dmamode   = false;
  priv->dmafastready = false;
#endif

  /* Configure the FIFO so that we will receive the TXDR interrupt whenever
   * there the TX FIFO is at least half empty.
   */

  rk3506_putreg(SDMMC_FIFOTH_TXWMARK(RK3506_TXFIFO_DEPTH / 2),
               RK3506_SDMMC_FIFOTH);

#ifdef CONFIG_RK3506_SDMMC_DMA
  /* Make sure that internal DMA is disabled */

  rk3506_putreg(0, RK3506_SDMMC_BMOD);

  regval  = rk3506_getreg(RK3506_SDMMC_CTRL);
  regval &= ~(SDMMC_CTRL_INTDMA | SDMMC_CTRL_DMAENABLE);
  rk3506_putreg(regval, RK3506_SDMMC_CTRL);
#endif

  /* Flush ints before we start */

  rk3506_putreg(SDCARD_TRANSFER_ALL, RK3506_SDMMC_RINTSTS);

  /* This controller advertises SDIO_CAPS_DMABEFOREWRITE, so sendsetup runs
   * before CMD24.  Use that ordering to prime the FIFO before the card data
   * state machine starts.  A 512-byte sector fits entirely in RK3506's
   * 256-word FIFO; larger requests leave the remainder to TXDR interrupts.
   */

  status = rk3506_getreg(RK3506_SDMMC_STATUS);
  while (priv->remaining >= sizeof(uint32_t) &&
         ((status & SDMMC_STATUS_FIFOCOUNT_MASK) >>
          SDMMC_STATUS_FIFOCOUNT_SHIFT) < RK3506_TXFIFO_DEPTH)
    {
      rk3506_putreg(*priv->buffer, RK3506_SDMMC_DATA);
      priv->buffer++;
      priv->remaining -= sizeof(uint32_t);
      status = rk3506_getreg(RK3506_SDMMC_STATUS);
    }

  /* Configure the transfer interrupts */

  rk3506_config_xfrints(priv, SDCARD_SEND_MASK);
  return OK;
}

/****************************************************************************
 * Name: rk3506_cancel
 *
 * Description:
 *   Cancel the data transfer setup of SDCARD_RECVSETUP, SDCARD_SENDSETUP,
 *   SDCARD_DMARECVSETUP or SDCARD_DMASENDSETUP.  This must be called to
 *   cancel the data transfer setup if, for some reason, you cannot perform
 *   the transfer.
 *
 * Input Parameters:
 *   dev  - An instance of the SD card device interface
 *
 * Returned Value:
 *   OK is success; a negated errno on failure
 *
 ****************************************************************************/

static int rk3506_cancel(struct sdio_dev_s *dev)
{
  struct rk3506_dev_s *priv = (struct rk3506_dev_s *)dev;

  mcinfo("Cancelling..\n");

  /* Disable all transfer- and event- related interrupts */

  rk3506_disable_allints(priv);

#ifdef CONFIG_RK3506_SDMMC_DMA
  if (priv->dmamode)
    {
      rk3506_dma_stop(priv);
    }

  priv->dmafastready = false;
#endif

  /* Clearing pending interrupt status on all transfer- and event- related
   * interrupts
   */

  rk3506_putreg(SDCARD_WAITALL_CLEAR, RK3506_SDMMC_RINTSTS);

  /* Cancel any watchdog timeout */

  wd_cancel(&priv->waitwdog);

  /* Mark no transfer in progress */

  priv->remaining = 0;
  return OK;
}

/****************************************************************************
 * Name: rk3506_waitresponse
 *
 * Description:
 *   Poll-wait for the response to the last command to be ready.
 *
 * Input Parameters:
 *   dev  - An instance of the SD card device interface
 *   cmd  - The command that was sent.  See 32-bit command definitions above.
 *
 * Returned Value:
 *   OK is success; a negated errno on failure
 *
 ****************************************************************************/

static int rk3506_waitresponse(struct sdio_dev_s *dev, uint32_t cmd)
{
  volatile int32_t timeout;
  clock_t watchtime;
  uint32_t events;

  mcinfo("cmd=%04lx\n", (unsigned long)cmd);

  switch (cmd & MMCSD_RESPONSE_MASK)
    {
    case MMCSD_NO_RESPONSE:
      events  = SDCARD_CMDDONE_STA;
      timeout = SDCARD_CMDTIMEOUT;
      break;

    case MMCSD_R1_RESPONSE:
    case MMCSD_R1B_RESPONSE:
    case MMCSD_R2_RESPONSE:
    case MMCSD_R6_RESPONSE:
      events  = (SDCARD_CMDDONE_STA | SDCARD_RESPDONE_STA);
      timeout = SDCARD_LONGTIMEOUT;
      break;

    case MMCSD_R4_RESPONSE:
    case MMCSD_R5_RESPONSE:
      return -ENOSYS;

    case MMCSD_R3_RESPONSE:
    case MMCSD_R7_RESPONSE:
      events  = (SDCARD_CMDDONE_STA | SDCARD_RESPDONE_STA);
      timeout = SDCARD_CMDTIMEOUT;
      break;

    default:
      return -EINVAL;
    }

  mcinfo("cmd: %04lx events: %04lx STATUS: %08lx RINTSTS: %08lx\n",
         (unsigned long)cmd, (unsigned long)events,
         (unsigned long)rk3506_getreg(RK3506_SDMMC_STATUS),
         (unsigned long)rk3506_getreg(RK3506_SDMMC_RINTSTS));

  /* Then wait for the response (or timeout or error) */

  watchtime = clock_systime_ticks();
  while ((rk3506_getreg(RK3506_SDMMC_RINTSTS) & events) != events)
    {
      if (clock_systime_ticks() - watchtime > timeout)
        {
          syslog(LOG_ERR,
                 "SDMMC: CMD%lu timeout status=%08lx rintsts=%08lx\n",
                 (unsigned long)((cmd & MMCSD_CMDIDX_MASK) >>
                                 MMCSD_CMDIDX_SHIFT),
                 (unsigned long)rk3506_getreg(RK3506_SDMMC_STATUS),
                 (unsigned long)rk3506_getreg(RK3506_SDMMC_RINTSTS));
          mcerr("ERROR: Timeout cmd: %04lx events: %04lx STA: %08lx "
                "RINTSTS: %08lx\n",
                (unsigned long)cmd, (unsigned long)events,
                (unsigned long)rk3506_getreg(RK3506_SDMMC_STATUS),
                (unsigned long)rk3506_getreg(RK3506_SDMMC_RINTSTS));

          return -ETIMEDOUT;
        }
      else if ((rk3506_getreg(RK3506_SDMMC_RINTSTS) & SDCARD_INT_RESPERR) != 0)
        {
          syslog(LOG_ERR,
                 "SDMMC: CMD%lu response error status=%08lx rintsts=%08lx\n",
                 (unsigned long)((cmd & MMCSD_CMDIDX_MASK) >>
                                 MMCSD_CMDIDX_SHIFT),
                 (unsigned long)rk3506_getreg(RK3506_SDMMC_STATUS),
                 (unsigned long)rk3506_getreg(RK3506_SDMMC_RINTSTS));
          mcerr("ERROR: SDMMC failure cmd: %04lx events: %04lx STA: %08lx "
                "RINTSTS: %08lx\n",
                (unsigned long)cmd, (unsigned long)events,
                (unsigned long)rk3506_getreg(RK3506_SDMMC_STATUS),
                (unsigned long)rk3506_getreg(RK3506_SDMMC_RINTSTS));

          return -EIO;
        }
    }

  rk3506_putreg(SDCARD_CMDDONE_CLEAR, RK3506_SDMMC_RINTSTS);
  return OK;
}

/****************************************************************************
 * Name: rk3506_recv*
 *
 * Description:
 *   Receive response to SD card command.  Only the critical payload is
 *   returned -- that is 32 bits for 48 bit status and 128 bits for 136 bit
 *   status.  The driver implementation should verify the correctness of
 *   the remaining, non-returned bits (CRCs, CMD index, etc.).
 *
 * Input Parameters:
 *   dev    - An instance of the SD card device interface
 *   Rx - Buffer in which to receive the response
 *
 * Returned Value:
 *   Number of bytes sent on success; a negated errno on failure.  Here a
 *   failure means only a faiure to obtain the requested response (due to
 *   transport problem -- timeout, CRC, etc.).  The implementation only
 *   assures that the response is returned intacta and does not check errors
 *   within the response itself.
 *
 ****************************************************************************/

static int rk3506_recvshortcrc(struct sdio_dev_s *dev, uint32_t cmd,
                              uint32_t *rshort)
{
  uint32_t regval;

  int ret = OK;

  mcinfo("cmd=%04lx\n", (unsigned long)cmd);

  /* R1  Command response (48-bit)
   *     47        0               Start bit
   *     46        0               Transmission bit (0=from card)
   *     45:40     bit5   - bit0   Command index (0-63)
   *     39:8      bit31  - bit0   32-bit card status
   *     7:1       bit6   - bit0   CRC7
   *     0         1               End bit
   *
   * R1b Identical to R1 with the additional busy signaling via the data
   *     line.
   *
   * R6  Published RCA Response (48-bit, SD card only)
   *     47        0               Start bit
   *     46        0               Transmission bit (0=from card)
   *     45:40     bit5   - bit0   Command index (0-63)
   *     39:8      bit31  - bit0   32-bit Argument Field, consisting of:
   *                               [31:16] New published RCA of card
   *                               [15:0]  Card status bits {23,22,19,12:0}
   *     7:1       bit6   - bit0   CRC7
   *     0         1               End bit
   */

#ifdef CONFIG_DEBUG_FEATURES
  if (!rshort)
    {
      mcerr("ERROR: rshort=NULL\n");
      ret = -EINVAL;
    }

  /* Check that this is the correct response to this command */

  else if ((cmd & MMCSD_RESPONSE_MASK) != MMCSD_R1_RESPONSE &&
           (cmd & MMCSD_RESPONSE_MASK) != MMCSD_R1B_RESPONSE &&
           (cmd & MMCSD_RESPONSE_MASK) != MMCSD_R6_RESPONSE)
    {
      mcerr("ERROR: Wrong response CMD=%04lx\n", (unsigned long)cmd);
      ret = -EINVAL;
    }
  else
#endif
    {
      /* Check if a timeout or CRC error occurred */

      regval = rk3506_getreg(RK3506_SDMMC_RINTSTS);
      if ((regval & SDMMC_INT_RTO) != 0)
        {
          mcerr("ERROR: Command timeout: %08lx\n", (unsigned long)regval);
          ret = -ETIMEDOUT;
        }
      else if ((regval & SDMMC_INT_RCRC) != 0)
        {
          mcerr("ERROR: CRC failure: %08lx\n", (unsigned long)regval);
          ret = -EIO;
        }
    }

  /* Clear all pending message completion events and return the R1/R6
   * response.
   */

  rk3506_putreg(SDCARD_RESPDONE_CLEAR | SDCARD_CMDDONE_CLEAR,
               RK3506_SDMMC_RINTSTS);
  *rshort = rk3506_getreg(RK3506_SDMMC_RESP0);
  mcinfo("CRC=%04lx\n", (unsigned long)*rshort);

  return ret;
}

static int rk3506_recvlong(struct sdio_dev_s *dev, uint32_t cmd,
                          uint32_t rlong[4])
{
  uint32_t regval;
  int ret = OK;

  mcinfo("cmd=%04lx\n", (unsigned long)cmd);

  /* R2  CID, CSD register (136-bit)
   *     135       0               Start bit
   *     134       0               Transmission bit (0=from card)
   *     133:128   bit5   - bit0   Reserved
   *     127:1     bit127 - bit1   127-bit CID or CSD register
   *                               (including internal CRC)
   *     0         1               End bit
   */

#ifdef CONFIG_DEBUG_FEATURES
  /* Check that R1 is the correct response to this command */

  if ((cmd & MMCSD_RESPONSE_MASK) != MMCSD_R2_RESPONSE)
    {
      mcerr("ERROR: Wrong response CMD=%04lx\n", (unsigned long)cmd);
      ret = -EINVAL;
    }
  else
#endif
    {
      /* Check if a timeout or CRC error occurred */

      regval = rk3506_getreg(RK3506_SDMMC_RINTSTS);
      if (regval & SDMMC_INT_RTO)
        {
          mcerr("ERROR: Timeout STA: %08lx\n", (unsigned long)regval);
          ret = -ETIMEDOUT;
        }
      else if (regval & SDMMC_INT_RCRC)
        {
          mcerr("ERROR: CRC fail STA: %08lx\n", (unsigned long)regval);
          ret = -EIO;
        }
    }

  /* Return the long response */

  rk3506_putreg(SDCARD_RESPDONE_CLEAR | SDCARD_CMDDONE_CLEAR,
               RK3506_SDMMC_RINTSTS);
  if (rlong)
    {
      rlong[0] = rk3506_getreg(RK3506_SDMMC_RESP3);
      rlong[1] = rk3506_getreg(RK3506_SDMMC_RESP2);
      rlong[2] = rk3506_getreg(RK3506_SDMMC_RESP1);
      rlong[3] = rk3506_getreg(RK3506_SDMMC_RESP0);
    }

  return ret;
}

static int rk3506_recvshort(struct sdio_dev_s *dev, uint32_t cmd,
                           uint32_t *rshort)
{
  uint32_t regval;
  int ret = OK;

  mcinfo("cmd=%04lx\n", (unsigned long)cmd);

  /* R3  OCR (48-bit)
   *     47        0               Start bit
   *     46        0               Transmission bit (0=from card)
   *     45:40     bit5   - bit0   Reserved
   *     39:8      bit31  - bit0   32-bit OCR register
   *     7:1       bit6   - bit0   Reserved
   *     0         1               End bit
   */

  /* Check that this is the correct response to this command */

#ifdef CONFIG_DEBUG_FEATURES
  if ((cmd & MMCSD_RESPONSE_MASK) != MMCSD_R3_RESPONSE &&
      (cmd & MMCSD_RESPONSE_MASK) != MMCSD_R7_RESPONSE)
    {
      mcerr("ERROR: Wrong response CMD=%04lx\n", (unsigned long)cmd);
      ret = -EINVAL;
    }
  else
#endif
    {
      /* Check if a timeout occurred (Apparently a CRC error can terminate
       * a good response)
       */

      regval = rk3506_getreg(RK3506_SDMMC_RINTSTS);
      if (regval & SDMMC_INT_RTO)
        {
          mcerr("ERROR: Timeout STA: %08lx\n", (unsigned long)regval);
          ret = -ETIMEDOUT;
        }
      else if ((cmd & MMCSD_RESPONSE_MASK) == MMCSD_R7_RESPONSE &&
               (regval & SDMMC_INT_RCRC) != 0)
        {
          mcerr("ERROR: CRC fail STA: %08lx\n", (unsigned long)regval);
          ret = -EIO;
        }
    }

  rk3506_putreg(SDCARD_RESPDONE_CLEAR | SDCARD_CMDDONE_CLEAR,
               RK3506_SDMMC_RINTSTS);
  if (rshort)
    {
      *rshort = rk3506_getreg(RK3506_SDMMC_RESP0);
    }

  return ret;
}

/* MMC responses not supported */

static int rk3506_recvnotimpl(struct sdio_dev_s *dev, uint32_t cmd,
                             uint32_t *rnotimpl)
{
  mcinfo("cmd=%04lx\n", (unsigned long)cmd);

  rk3506_putreg(SDCARD_RESPDONE_CLEAR | SDCARD_CMDDONE_CLEAR,
               RK3506_SDMMC_RINTSTS);
  return -ENOSYS;
}

/****************************************************************************
 * Name: rk3506_waitenable
 *
 * Description:
 *   Enable/disable of a set of SD card wait events.  This is part of the
 *   the SDCARD_WAITEVENT sequence.  The set of to-be-waited-for events is
 *   configured before calling rk3506_eventwait.  This is done in this way
 *   to help the driver to eliminate race conditions between the command
 *   setup and the subsequent events.
 *
 *   The enabled events persist until either (1) SDCARD_WAITENABLE is called
 *   again specifying a different set of wait events, or (2) SDCARD_EVENTWAIT
 *   returns.
 *
 * Input Parameters:
 *   dev      - An instance of the SD card device interface
 *   eventset - A bitset of events to enable or disable (see SDIOWAIT_*
 *              definitions). 0=disable; 1=enable.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void rk3506_waitenable(struct sdio_dev_s *dev,
                             sdio_eventset_t eventset, uint32_t timeout)
{
  struct rk3506_dev_s *priv = (struct rk3506_dev_s *)dev;
  uint32_t waitmask;

  mcinfo("eventset=%04x\n", (unsigned int)eventset);
  DEBUGASSERT(priv != NULL);

  /* Disable event-related interrupts */

  rk3506_config_waitints(priv, 0, 0, 0);

  /* Select the interrupt mask that will give us the appropriate wakeup
   * interrupts.
   */

  waitmask = 0;
  if ((eventset & SDIOWAIT_CMDDONE) != 0)
    {
      waitmask |= SDCARD_CMDDONE_MASK;
    }

  if ((eventset & SDIOWAIT_RESPONSEDONE) != 0)
    {
      waitmask |= SDCARD_RESPDONE_MASK;
    }

  if ((eventset & SDIOWAIT_TRANSFERDONE) != 0)
    {
      waitmask |= SDCARD_XFRDONE_MASK;
    }

  /* Enable event-related interrupts */

  rk3506_config_waitints(priv, waitmask, eventset, 0);

  /* Check if the timeout event is specified in the event set */

  if ((priv->waitevents & SDIOWAIT_TIMEOUT) != 0)
    {
      int delay;
      int ret;

      /* Yes.. Handle a cornercase: The user request a timeout event but
       * with timeout == 0?
       */

      if (!timeout)
        {
          priv->wkupevent = SDIOWAIT_TIMEOUT;
          return;
        }

      /* Start the watchdog timer */

      delay = MSEC2TICK(timeout);
      ret   = wd_start(&priv->waitwdog, delay,
                       rk3506_eventtimeout, (wdparm_t)priv);
      if (ret < 0)
        {
          mcerr("ERROR: wd_start failed: %d\n", ret);
        }
    }
}

/****************************************************************************
 * Name: rk3506_eventwait
 *
 * Description:
 *   Wait for one of the enabled events to occur (or a timeout).  Note that
 *   all events enabled by SDCARD_WAITEVENTS are disabled when
 *   rk3506_eventwait returns.  SDCARD_WAITEVENTS must be called again
 *   before rk3506_eventwait can be used again.
 *
 * Input Parameters:
 *   dev     - An instance of the SD card device interface
 *   timeout - Maximum time in milliseconds to wait.  Zero means immediate
 *             timeout with no wait.  The timeout value is ignored if
 *             SDIOWAIT_TIMEOUT is not included in the waited-for eventset.
 *
 * Returned Value:
 *   Event set containing the event(s) that ended the wait.  Should always
 *   be non-zero.  All events are disabled after the wait concludes.
 *
 ****************************************************************************/

static sdio_eventset_t rk3506_eventwait(struct sdio_dev_s *dev)
{
  struct rk3506_dev_s *priv = (struct rk3506_dev_s *)dev;
  sdio_eventset_t wkupevent = 0;
  irqstate_t flags;
  int ret;

  /* There is a race condition here... the event may have completed before
   * we get here.  In this case waitevents will be zero, but wkupevents will
   * be non-zero (and, hopefully, the semaphore count will also be non-zero.
   */

  flags = enter_critical_section();
  DEBUGASSERT(priv->waitevents != 0 || priv->wkupevent != 0);

  /* Loop until the event (or the timeout occurs). Race conditions are
   * avoided by calling rk3506_waitenable prior to triggering the logic that
   * will cause the wait to terminate.  Under certain race conditions, the
   * waited-for may have already occurred before this function was called!
   */

  for (; ; )
    {
      /* Wait for an event in event set to occur.  If this the event has
       * already occurred, then the semaphore will already have been
       * incremented and there will be no wait.
       */

      ret = nxsem_wait_uninterruptible(&priv->waitsem);
      if (ret < 0)
        {
          /* Task canceled.  Cancel the wdog (assuming it was started) and
           * return an SDIO error.
           */

          wd_cancel(&priv->waitwdog);
          leave_critical_section(flags);
          return SDIOWAIT_ERROR;
        }

      wkupevent = priv->wkupevent;

      /* Check if the event has occurred.  When the event has occurred, then
       * evenset will be set to 0 and wkupevent will be set to a nonzero
       * value.
       */

      if (wkupevent != 0)
        {
          /* Yes... break out of the loop with wkupevent non-zero */

          break;
        }
    }

  /* Disable all transfer- and event- related interrupts */

  rk3506_disable_allints(priv);

  leave_critical_section(flags);
  mcinfo("wkupevent=%04x\n", wkupevent);
  return wkupevent;
}

/****************************************************************************
 * Name: rk3506_callbackenable
 *
 * Description:
 *   Enable/disable of a set of SD card callback events.  This is part of
 *   the the SD card callback sequence.  The set of events is configured to
 *   enabled callbacks to the function provided in rk3506_registercallback.
 *
 *   Events are automatically disabled once the callback is performed and no
 *   further callback events will occur until they are again enabled by
 *   calling this method.
 *
 * Input Parameters:
 *   dev      - An instance of the SD card device interface
 *   eventset - A bitset of events to enable or disable (see SDIOMEDIA_*
 *              definitions). 0=disable; 1=enable.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void rk3506_callbackenable(struct sdio_dev_s *dev,
                                 sdio_eventset_t eventset)
{
  struct rk3506_dev_s *priv = (struct rk3506_dev_s *)dev;

  mcinfo("eventset: %02x\n", eventset);
  DEBUGASSERT(priv != NULL);

  priv->cbevents = eventset;
  rk3506_callback(priv);
}

/****************************************************************************
 * Name: rk3506_registercallback
 *
 * Description:
 *   Register a callback that that will be invoked on any media status
 *   change.  Callbacks should not be made from interrupt handlers, rather
 *   interrupt level events should be handled by calling back on the work
 *   thread.
 *
 *   When this method is called, all callbacks should be disabled until they
 *   are enabled via a call to SDCARD_CALLBACKENABLE
 *
 * Input Parameters:
 *   dev -      Device-specific state data
 *   callback - The function to call on the media change
 *   arg -      A caller provided value to return with the callback
 *
 * Returned Value:
 *   0 on success; negated errno on failure.
 *
 ****************************************************************************/

static int rk3506_registercallback(struct sdio_dev_s *dev,
                                  worker_t callback, void *arg)
{
  struct rk3506_dev_s *priv = (struct rk3506_dev_s *)dev;

  mcinfo("callback=%p arg=%p\n", callback, arg);

  /* Disable callbacks and register this callback and its argument */

  mcinfo("Register %p(%p)\n", callback, arg);
  DEBUGASSERT(priv != NULL);

  priv->cbevents = 0;
  priv->cbarg    = arg;
  priv->callback = callback;
  return OK;
}

#ifdef CONFIG_RK3506_SDMMC_DMA

/****************************************************************************
 * Name: rk3506_dma_stop
 ****************************************************************************/

static void rk3506_dma_stop(struct rk3506_dev_s *priv)
{
  uint32_t regval;

  rk3506_putreg(0, RK3506_SDMMC_IDINTEN);

  regval  = rk3506_getreg(RK3506_SDMMC_BMOD);
  regval &= ~SDMMC_BMOD_DE;
  rk3506_putreg(regval, RK3506_SDMMC_BMOD);

  regval  = rk3506_getreg(RK3506_SDMMC_CTRL);
  regval &= ~(SDMMC_CTRL_INTDMA | SDMMC_CTRL_DMAENABLE);
  rk3506_putreg(regval, RK3506_SDMMC_CTRL);

  rk3506_putreg(SDMMC_IDINTEN_ALL, RK3506_SDMMC_IDSTS);

  priv->dmamode   = false;
  priv->dmabounce = false;
  priv->dmabuffer = NULL;
  priv->dmalen    = 0;
}

/****************************************************************************
 * Name: rk3506_dmasetup
 ****************************************************************************/

static int rk3506_dmasetup(struct rk3506_dev_s *priv,
                           uint8_t *buffer, size_t buflen, bool write)
{
  struct sdmmc_dma_s *desc;
  uint8_t *dmabuffer;
  uintptr_t dmaend;
  uint32_t regval;
  uint32_t ctrl;
  size_t remaining;
  size_t offset;
  size_t maxs;
  unsigned int ndesc;
  unsigned int i;
  bool fastreuse;

  DEBUGASSERT(priv != NULL);

  if (buffer == NULL || buflen == 0 ||
      (((uintptr_t)buffer | buflen) & 3) != 0)
    {
      return -EINVAL;
    }

  /* Keep short requests in the proven PIO path.  Requests too large for the
   * static descriptor/bounce area also remain functional through PIO.
   */

  /* IDMAC moves data between system memory and the controller FIFO; it is
   * independent of whether the card side currently uses one or four data
   * lines.  Requiring widebus here silently forced all traffic back to PIO
   * on cards that remained in 1-bit mode.
   */

  if (buflen < RK3506_IDMAC_MINSIZE ||
      buflen > RK3506_IDMAC_MAXSIZE)
    {
      return write ?
             rk3506_sendsetup(&priv->dev, buffer, buflen) :
             rk3506_recvsetup(&priv->dev, buffer, buflen);
    }

  /* The RK3506 port currently uses an identity-mapped flat address space.
   * Use a cache-line-aligned bounce buffer when either end of the caller's
   * buffer shares a cache line with unrelated data.
   */

  dmaend = (uintptr_t)buffer + buflen;
  if ((((uintptr_t)buffer | dmaend) &
       (RK3506_DCACHE_LINESIZE - 1)) != 0)
    {
      dmabuffer = g_sdmmc_dmabounce;
      priv->dmabounce = true;
    }
  else
    {
      dmabuffer = buffer;
      priv->dmabounce = false;
    }

  if (write)
    {
      if (priv->dmabounce)
        {
          memcpy(dmabuffer, buffer, buflen);
        }

      up_clean_dcache((uintptr_t)dmabuffer,
                      (uintptr_t)dmabuffer + buflen);
    }
  else
    {
      up_invalidate_dcache((uintptr_t)dmabuffer,
                           (uintptr_t)dmabuffer + buflen);
    }

  /* A cleanly completed DMA leaves the bus master stopped and its FIFO
   * drained.  Reuse that state while still resetting the FIFO defensively.
   * Initialization, PIO, cancellation and every error force the full reset
   * path.  If this optional engine does not reset cleanly, preserve storage
   * access through PIO.
   */

  fastreuse = priv->dmafastready;
  priv->dmafastready = false;

  if (!fastreuse)
    {
      rk3506_putreg(SDMMC_BMOD_SWR, RK3506_SDMMC_BMOD);
      if (!rk3506_wait_clear(RK3506_SDMMC_BMOD, SDMMC_BMOD_SWR,
                             "IDMAC bus reset"))
        {
          return write ?
                 rk3506_sendsetup(&priv->dev, buffer, buflen) :
                 rk3506_recvsetup(&priv->dev, buffer, buflen);
        }
    }

  regval  = rk3506_getreg(RK3506_SDMMC_CTRL);
  regval |= SDMMC_CTRL_FIFORESET;
  if (!fastreuse)
    {
      regval |= SDMMC_CTRL_DMARESET;
    }

  rk3506_putreg(regval, RK3506_SDMMC_CTRL);

  if (!rk3506_wait_clear(RK3506_SDMMC_CTRL,
                         SDMMC_CTRL_FIFORESET |
                         (fastreuse ? 0 : SDMMC_CTRL_DMARESET),
                         "IDMAC FIFO/DMA reset"))
    {
      return write ?
             rk3506_sendsetup(&priv->dev, buffer, buflen) :
             rk3506_recvsetup(&priv->dev, buffer, buflen);
    }

  if (fastreuse && (priv->dmatrace & RK3506_DMA_TRACE_REUSE) == 0)
    {
      priv->dmatrace |= RK3506_DMA_TRACE_REUSE;
      syslog(LOG_INFO, "SDMMC: IDMAC fast reuse active\n");
    }

  regval = (write ?
            SDMMC_FIFOTH_TXWMARK(RK3506_TXFIFO_DEPTH / 2) :
            SDMMC_FIFOTH_RXWMARK(RK3506_RXFIFO_DEPTH / 2 - 1)) |
           SDMMC_FIFOTH_DMABURST_4XFRS;
  rk3506_putreg(regval, RK3506_SDMMC_FIFOTH);

  ndesc = (buflen + MCI_DMADES1_MAXTR - 1) / MCI_DMADES1_MAXTR;
  DEBUGASSERT(ndesc > 0 && ndesc <= NUM_DMA_DESCRIPTORS);

  memset(g_sdmmc_dmadd, 0, ndesc * sizeof(struct sdmmc_dma_s));
  remaining = buflen;
  offset = 0;

  for (i = 0; i < ndesc; i++)
    {
      desc = &g_sdmmc_dmadd[i];
      maxs = remaining > MCI_DMADES1_MAXTR ?
             MCI_DMADES1_MAXTR : remaining;

      ctrl = MCI_DMADES0_OWN | MCI_DMADES0_CH;
      if (i == 0)
        {
          ctrl |= MCI_DMADES0_FS;
        }

      if (i + 1 == ndesc)
        {
          ctrl |= MCI_DMADES0_LD;
          desc->des3 = 0;
        }
      else
        {
          ctrl |= MCI_DMADES0_DIC;
          desc->des3 = (uint32_t)&g_sdmmc_dmadd[i + 1];
        }

      desc->des1 = MCI_DMADES1_BS1(maxs);
      desc->des2 = (uint32_t)dmabuffer + offset;
      desc->des0 = ctrl;
      remaining -= maxs;
      offset += maxs;
    }

  up_clean_dcache((uintptr_t)g_sdmmc_dmadd,
                  (uintptr_t)g_sdmmc_dmadd +
                  ndesc * sizeof(struct sdmmc_dma_s));

  priv->buffer    = (uint32_t *)buffer;
  priv->remaining = buflen;
  priv->wrdir     = write;
  priv->dmamode   = true;
  priv->dmabuffer = dmabuffer;
  priv->dmalen    = buflen;

  rk3506_putreg(SDCARD_TRANSFER_ALL, RK3506_SDMMC_RINTSTS);
  rk3506_putreg(SDMMC_IDINTEN_ALL, RK3506_SDMMC_IDSTS);
  rk3506_putreg((uint32_t)g_sdmmc_dmadd, RK3506_SDMMC_DBADDR);

  regval = rk3506_getreg(RK3506_SDMMC_CTRL);
  regval |= SDMMC_CTRL_INTDMA | SDMMC_CTRL_DMAENABLE;
  rk3506_putreg(regval, RK3506_SDMMC_CTRL);

  regval = SDMMC_BMOD_FB | SDMMC_BMOD_DE | SDMMC_BMOD_PBL_4XFRS;
  rk3506_putreg(regval, RK3506_SDMMC_BMOD);

  rk3506_config_dmaints(priv,
                        write ? SDCARD_DMASEND_MASK : SDCARD_DMARECV_MASK,
                        SDCARD_DMAERROR_MASK);

  if (write && (priv->dmatrace & RK3506_DMA_TRACE_WRITE) == 0)
    {
      priv->dmatrace |= RK3506_DMA_TRACE_WRITE;
      syslog(LOG_INFO, "SDMMC: IDMAC write active (%lu bytes%s)\n",
             (unsigned long)buflen,
             priv->dmabounce ? ", bounce" : "");
    }
  else if (!write && (priv->dmatrace & RK3506_DMA_TRACE_READ) == 0)
    {
      priv->dmatrace |= RK3506_DMA_TRACE_READ;
      syslog(LOG_INFO, "SDMMC: IDMAC read active (%lu bytes%s)\n",
             (unsigned long)buflen,
             priv->dmabounce ? ", bounce" : "");
    }

  if (write && buflen > priv->dmamaxwrite)
    {
      priv->dmamaxwrite = buflen;
      syslog(LOG_INFO, "SDMMC: IDMAC write max request now %lu bytes%s\n",
             (unsigned long)buflen,
             priv->dmabounce ? ", bounce" : "");
    }
  else if (!write && buflen > priv->dmamaxread)
    {
      priv->dmamaxread = buflen;
      syslog(LOG_INFO, "SDMMC: IDMAC read max request now %lu bytes%s\n",
             (unsigned long)buflen,
             priv->dmabounce ? ", bounce" : "");
    }

  return OK;
}

/****************************************************************************
 * Name: rk3506_dmarecvsetup
 ****************************************************************************/

static int rk3506_dmarecvsetup(struct sdio_dev_s *dev,
                               uint8_t *buffer, size_t buflen)
{
  return rk3506_dmasetup((struct rk3506_dev_s *)dev,
                         buffer, buflen, false);
}

/****************************************************************************
 * Name: rk3506_dmasendsetup
 ****************************************************************************/

static int rk3506_dmasendsetup(struct sdio_dev_s *dev,
                               const uint8_t *buffer, size_t buflen)
{
  return rk3506_dmasetup((struct rk3506_dev_s *)dev,
                         (uint8_t *)buffer, buflen, true);
}
#endif

/****************************************************************************
 * Name: rk3506_callback
 *
 * Description:
 *   Perform callback.
 *
 * Assumptions:
 *   This function does not execute in the context of an interrupt handler.
 *   It may be invoked on any user thread or scheduled on the work thread
 *   from an interrupt handler.
 *
 ****************************************************************************/

static void rk3506_callback(struct rk3506_dev_s *priv)
{
  /* Is a callback registered? */

  mcinfo("Callback %p(%p) cbevents: %02x cdstatus: %02x\n",
         priv->callback, priv->cbarg, priv->cbevents, priv->cdstatus);

  if (priv->callback)
    {
      /* Yes.. Check for enabled callback events */

      if ((priv->cdstatus & SDIO_STATUS_PRESENT) != 0)
        {
          /* Media is present.  Is the media inserted event enabled? */

          if ((priv->cbevents & SDIOMEDIA_INSERTED) == 0)
            {
              /* No... return without performing the callback */

              return;
            }
        }
      else
        {
          /* Media is not present.  Is the media eject event enabled? */

          if ((priv->cbevents & SDIOMEDIA_EJECTED) == 0)
            {
              /* No... return without performing the callback */

              return;
            }
        }

      /* Perform the callback, disabling further callbacks.  Of course, the
       * the callback can (and probably should) re-enable callbacks.
       */

      priv->cbevents = 0;

      /* Callbacks cannot be performed in the context of an interrupt
       * handler.  If we are in an interrupt handler, then queue the
       * callback to be performed later on the work thread.
       */

      if (up_interrupt_context())
        {
          /* Yes.. queue it */

          mcinfo("Queuing callback to %p(%p)\n",
                 priv->callback, priv->cbarg);
          work_queue(HPWORK, &priv->cbwork, priv->callback,
                     priv->cbarg, 0);
        }
      else
        {
          /* No.. then just call the callback here */

          mcinfo("Callback to %p(%p)\n", priv->callback, priv->cbarg);
          priv->callback(priv->cbarg);
        }
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rk3506_sdmmc_initialize
 *
 * Description:
 *   Initialize the SD/MMC peripheral for normal operation.
 *
 * Input Parameters:
 *   slotno - Not used.
 *
 * Returned Value:
 *   A reference to an SD card interface structure.  NULL is returned on
 *   failures.
 *
 ****************************************************************************/

struct sdio_dev_s *rk3506_sdmmc_initialize(int slotno)
{
  struct rk3506_dev_s *priv = &g_scard_dev;

  mcinfo("slotno=%d\n", slotno);

  /* Zero in a Rockchip clock-gate bit means enabled. */

  rk3506_putreg(RK3506_WRITE_MASK(RK3506_SDMMC_CLOCK_GATES, 0),
                RK3506_CRU_GATE_CON17);
  rk3506_putreg(RK3506_WRITE_MASK(RK3506_GPIO234_IOC_GATE, 0),
                RK3506_CRU_GATE_CON19);

  /* GPIO3_A0..A5: CLK, CMD and D0..D3, mux function 1. */

  rk3506_putreg(RK3506_WRITE_MASK(0xffffu, 0x1111u),
                RK3506_GPIO3A_IOMUX_SEL_0);
  rk3506_putreg(RK3506_WRITE_MASK(0x00ffu, 0x0011u),
                RK3506_GPIO3A_IOMUX_SEL_1);

  /* Keep CLK without an internal pull and pull CMD/D0..D3 up. */

  rk3506_putreg(RK3506_WRITE_MASK(0x0fffu, 0x0554u),
                RK3506_GPIO3A_PULL);

  /* Reset the card and assure that it is in the initial, unconfigured
   * state.
   */

  rk3506_putreg(SDMMC_PWREN, RK3506_SDMMC_PWREN);
  rk3506_reset(&priv->dev);
  if (!priv->resetok)
    {
      return NULL;
    }

  /* Match Rockchip HAL ordering: a controller reset is followed by an
   * explicit card-power enable before protocol enumeration starts.
   */

  rk3506_putreg(SDMMC_PWREN, RK3506_SDMMC_PWREN);

  return &g_scard_dev.dev;
}

#endif /* CONFIG_RK3506_SDMMC */
