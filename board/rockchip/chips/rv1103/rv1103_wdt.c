/****************************************************************************
 * vendor/rockchip/chips/rv1103/rv1103_wdt.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_RV1103_WATCHDOG

#include <stdbool.h>
#include <stdint.h>
#include <errno.h>

#include <nuttx/timers/watchdog.h>

#include <arch/chip/rv1103_wdt.h>

#include "arm_internal.h"

/* RV1103 uses the Synopsys DesignWare watchdog described by the official
 * DTS as watchdog@ff5a0000.  Its input clock is the 24 MHz xin24m source.
 */

#define RV1103_WDT_BASE                 0xff5a0000u
#define RV1103_WDT_CR                   (RV1103_WDT_BASE + 0x00u)
#define RV1103_WDT_TORR                 (RV1103_WDT_BASE + 0x04u)
#define RV1103_WDT_CCVR                 (RV1103_WDT_BASE + 0x08u)
#define RV1103_WDT_CRR                  (RV1103_WDT_BASE + 0x0cu)

#define RV1103_WDT_CR_ENABLE            (1u << 0)
#define RV1103_WDT_RESTART_KEY          0x76u
#define RV1103_WDT_CLOCK                24000000u
#define RV1103_WDT_TOP_MAX              15u

/* PCLK_WDT_NS and TCLK_WDT_NS are PERI CRU gate 1 bits 2 and 3. */

#define RV1103_CRU_PERI_CLKGATE_CON1    0xff3b2804u
#define RV1103_PCLK_WDT_NS_SHIFT        2u
#define RV1103_TCLK_WDT_NS_SHIFT        3u

struct rv1103_wdt_lowerhalf_s
{
  struct watchdog_lowerhalf_s lower;
  uint32_t timeout;
  uint8_t top;
  bool started;
};

static int rv1103_wdt_start(struct watchdog_lowerhalf_s *lower);
static int rv1103_wdt_stop(struct watchdog_lowerhalf_s *lower);
static int rv1103_wdt_keepalive(struct watchdog_lowerhalf_s *lower);
static int rv1103_wdt_getstatus(struct watchdog_lowerhalf_s *lower,
                                struct watchdog_status_s *status);
static int rv1103_wdt_settimeout(struct watchdog_lowerhalf_s *lower,
                                 uint32_t timeout);
static int rv1103_wdt_ioctl(struct watchdog_lowerhalf_s *lower, int cmd,
                            unsigned long arg);

static const struct watchdog_ops_s g_rv1103_wdt_ops =
{
  .start      = rv1103_wdt_start,
  .stop       = rv1103_wdt_stop,
  .keepalive  = rv1103_wdt_keepalive,
  .getstatus  = rv1103_wdt_getstatus,
  .settimeout = rv1103_wdt_settimeout,
  .capture    = NULL,
  .ioctl      = rv1103_wdt_ioctl,
};

static struct rv1103_wdt_lowerhalf_s g_rv1103_wdt =
{
  .lower =
    {
      .ops = &g_rv1103_wdt_ops,
    },
  .timeout = 2796,
  .top = 10,
  .started = false,
};

static uint32_t rv1103_wdt_top_msec(unsigned int top)
{
  uint64_t cycles = 1ull << (16u + top);

  return (uint32_t)((cycles * 1000ull) / RV1103_WDT_CLOCK);
}

static void rv1103_wdt_enable_clocks(void)
{
  uint32_t mask;

  /* A zero Rockchip gate bit enables the clock. */

  mask = (1u << RV1103_PCLK_WDT_NS_SHIFT) |
         (1u << RV1103_TCLK_WDT_NS_SHIFT);
  putreg32(mask << 16, RV1103_CRU_PERI_CLKGATE_CON1);
}

static int rv1103_wdt_start(struct watchdog_lowerhalf_s *lower)
{
  struct rv1103_wdt_lowerhalf_s *priv =
    (struct rv1103_wdt_lowerhalf_s *)lower;

  putreg32(((uint32_t)priv->top << 4) | priv->top, RV1103_WDT_TORR);
  putreg32(RV1103_WDT_RESTART_KEY, RV1103_WDT_CRR);

  /* RMOD=0 selects a direct system reset without a first-stage interrupt. */

  putreg32(RV1103_WDT_CR_ENABLE, RV1103_WDT_CR);
  putreg32(RV1103_WDT_RESTART_KEY, RV1103_WDT_CRR);
  priv->started = true;
  return 0;
}

static int rv1103_wdt_stop(struct watchdog_lowerhalf_s *lower)
{
  struct rv1103_wdt_lowerhalf_s *priv =
    (struct rv1103_wdt_lowerhalf_s *)lower;

  /* DesignWare cannot be disabled after CR.EN is set; only reset clears it. */

  return priv->started ? -ENOSYS : 0;
}

static int rv1103_wdt_keepalive(struct watchdog_lowerhalf_s *lower)
{
  (void)lower;
  putreg32(RV1103_WDT_RESTART_KEY, RV1103_WDT_CRR);
  return 0;
}

static int rv1103_wdt_getstatus(struct watchdog_lowerhalf_s *lower,
                                struct watchdog_status_s *status)
{
  struct rv1103_wdt_lowerhalf_s *priv =
    (struct rv1103_wdt_lowerhalf_s *)lower;
  uint64_t cycles;

  if (status == NULL)
    {
      return -EINVAL;
    }

  status->flags = WDFLAGS_RESET;
  status->timeout = priv->timeout;

  if (priv->started)
    {
      status->flags |= WDFLAGS_ACTIVE;
      cycles = getreg32(RV1103_WDT_CCVR);
      status->timeleft =
        (uint32_t)((cycles * 1000ull) / RV1103_WDT_CLOCK);
    }
  else
    {
      status->timeleft = priv->timeout;
    }

  return 0;
}

static int rv1103_wdt_settimeout(struct watchdog_lowerhalf_s *lower,
                                 uint32_t timeout)
{
  struct rv1103_wdt_lowerhalf_s *priv =
    (struct rv1103_wdt_lowerhalf_s *)lower;
  uint64_t requested_cycles;
  uint64_t available_cycles;
  unsigned int top;

  if (timeout == 0)
    {
      return -ERANGE;
    }

  requested_cycles = (uint64_t)timeout * RV1103_WDT_CLOCK;
  for (top = 0; top <= RV1103_WDT_TOP_MAX; top++)
    {
      available_cycles = (1ull << (16u + top)) * 1000ull;
      if (available_cycles >= requested_cycles)
        {
          priv->top = (uint8_t)top;
          priv->timeout = rv1103_wdt_top_msec(top);
          putreg32((top << 4) | top, RV1103_WDT_TORR);
          if (priv->started)
            {
              putreg32(RV1103_WDT_RESTART_KEY, RV1103_WDT_CRR);
            }

          return 0;
        }
    }

  return -ERANGE;
}

static int rv1103_wdt_ioctl(struct watchdog_lowerhalf_s *lower, int cmd,
                            unsigned long arg)
{
  (void)lower;
  (void)cmd;
  (void)arg;
  return -ENOTTY;
}

int rv1103_wdt_initialize(void)
{
  rv1103_wdt_enable_clocks();

  if (watchdog_register("/dev/watchdog0", &g_rv1103_wdt.lower) == NULL)
    {
      return -ENODEV;
    }

  return 0;
}

#endif /* CONFIG_RV1103_WATCHDOG */
