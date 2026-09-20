/****************************************************************************
 * vendor/allwinnertech/chips/a733/a733_watchdog.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_A733_WATCHDOG

#include <stdbool.h>
#include <stdint.h>
#include <errno.h>

#include <nuttx/timers/watchdog.h>

#include "arm64_internal.h"

#define A733_WDT_BASE          UINT64_C(0x02050000)
#define A733_WDT_CTRL          (A733_WDT_BASE + 0x0c)
#define A733_WDT_CFG           (A733_WDT_BASE + 0x10)
#define A733_WDT_MODE          (A733_WDT_BASE + 0x14)
#define A733_WDT_MAGIC         0x16aa0000u
#define A733_WDT_RELOAD        ((0x0a57u << 1) | 1u)
#define A733_WDT_ENABLE        (1u << 0)
#define A733_WDT_INTV_SHIFT    4

struct a733_wdt_s
{
  struct watchdog_lowerhalf_s lower;
  uint32_t timeout;
  uint8_t interval;
  bool started;
};

static const uint8_t g_timeout_seconds[] =
{
  0, 1, 2, 3, 4, 5, 6, 8, 10, 12, 14, 16
};

static int a733_wdt_keepalive(struct watchdog_lowerhalf_s *lower)
{
  (void)lower;
  putreg32(A733_WDT_RELOAD, A733_WDT_CTRL);
  return 0;
}

static int a733_wdt_start(struct watchdog_lowerhalf_s *lower)
{
  struct a733_wdt_s *priv = (struct a733_wdt_s *)lower;
  uint32_t reg;

  reg = getreg32(A733_WDT_CFG);
  reg &= ~3u;
  putreg32(reg | A733_WDT_MAGIC | 1u, A733_WDT_CFG);

  reg = getreg32(A733_WDT_MODE);
  reg &= ~(0x0fu << A733_WDT_INTV_SHIFT);
  reg |= ((uint32_t)priv->interval << A733_WDT_INTV_SHIFT) |
         A733_WDT_MAGIC | A733_WDT_ENABLE;
  putreg32(reg, A733_WDT_MODE);
  a733_wdt_keepalive(lower);
  priv->started = true;
  return 0;
}

static int a733_wdt_stop(struct watchdog_lowerhalf_s *lower)
{
  struct a733_wdt_s *priv = (struct a733_wdt_s *)lower;

  putreg32(A733_WDT_MAGIC, A733_WDT_MODE);
  priv->started = false;
  return 0;
}

static int a733_wdt_getstatus(struct watchdog_lowerhalf_s *lower,
                              struct watchdog_status_s *status)
{
  struct a733_wdt_s *priv = (struct a733_wdt_s *)lower;

  if (status == NULL)
    {
      return -EINVAL;
    }

  status->flags = WDFLAGS_RESET | (priv->started ? WDFLAGS_ACTIVE : 0);
  status->timeout = priv->timeout;
  status->timeleft = priv->timeout;
  return 0;
}

static int a733_wdt_settimeout(struct watchdog_lowerhalf_s *lower,
                               uint32_t timeout)
{
  struct a733_wdt_s *priv = (struct a733_wdt_s *)lower;
  unsigned int i;

  for (i = 1; i < sizeof(g_timeout_seconds); i++)
    {
      if (timeout <= (uint32_t)g_timeout_seconds[i] * 1000u)
        {
          priv->interval = i;
          priv->timeout = (uint32_t)g_timeout_seconds[i] * 1000u;
          if (priv->started)
            {
              return a733_wdt_start(lower);
            }

          return 0;
        }
    }

  return -ERANGE;
}

static int a733_wdt_ioctl(struct watchdog_lowerhalf_s *lower, int cmd,
                          unsigned long arg)
{
  (void)lower;
  (void)cmd;
  (void)arg;
  return -ENOTTY;
}

static const struct watchdog_ops_s g_a733_wdt_ops =
{
  .start = a733_wdt_start,
  .stop = a733_wdt_stop,
  .keepalive = a733_wdt_keepalive,
  .getstatus = a733_wdt_getstatus,
  .settimeout = a733_wdt_settimeout,
  .capture = NULL,
  .ioctl = a733_wdt_ioctl,
};

static struct a733_wdt_s g_a733_wdt =
{
  .lower = { .ops = &g_a733_wdt_ops },
  .timeout = 16000,
  .interval = 11,
};

int a733_watchdog_initialize(void)
{
  /* Leave a watchdog inherited from firmware stopped.  Registration alone
   * is safe; the timer starts only after WDIOC_START/open policy requests it.
   */

  putreg32(A733_WDT_MAGIC, A733_WDT_MODE);
  return watchdog_register("/dev/watchdog0", &g_a733_wdt.lower) == NULL ?
         -ENODEV : 0;
}

#endif
