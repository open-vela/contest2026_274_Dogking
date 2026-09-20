/****************************************************************************
 * vendor/rockchip/chips/rv1103/rv1103_pwm.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_RV1103_PWM

#include <stdint.h>
#include <errno.h>
#include <nuttx/arch.h>
#include <nuttx/timers/pwm.h>
#include <arch/chip/rv1103_periph.h>

#include "arm_internal.h"

#define PWM_CLOCK       24000000u
#define PWM_PERIOD      0x04u
#define PWM_DUTY        0x08u
#define PWM_CTRL        0x0cu
#define PWM_CTRL_ENABLE (1u << 0)
#define PWM_CTRL_LOCK   (1u << 6)

struct rv1103_pwm_s
{
  FAR const struct pwm_ops_s *ops;
  uintptr_t base;
  uint8_t channel;
};

struct rv1103_pwm_pin_s
{
  uint8_t bank;
  uint8_t pin;
  uint8_t function;
};

/* Every route listed by the official rv1106-pinctrl.dtsi is retained here.
 * A board configuration selects exactly one route for the registered PWM;
 * this avoids silently stealing pins from SPI, UART, SFC or the work LED.
 */

static const struct rv1103_pwm_pin_s g_pwm_pins[12][3] =
{
  {{1,  2, 1}, {1, 26, 6}, {0xff, 0, 0}},
  {{0,  4, 2}, {4, 17, 2}, {3, 27, 2}},
  {{0,  1, 2}, {2,  6, 4}, {1, 16, 3}},
  {{0,  2, 1}, {1,  8, 2}, {1, 24, 3}},
  {{1,  1, 4}, {2,  7, 4}, {1, 17, 3}},
  {{0,  5, 3}, {2,  8, 4}, {1, 18, 3}},
  {{0,  6, 3}, {2,  9, 4}, {1, 19, 3}},
  {{1,  0, 3}, {1,  9, 2}, {3, 22, 2}},
  {{3,  3, 4}, {1, 20, 3}, {0xff, 0, 0}},
  {{3,  2, 4}, {1, 21, 3}, {0xff, 0, 0}},
  {{3,  4, 5}, {1, 22, 3}, {1, 25, 3}},
  {{3,  5, 5}, {1, 23, 3}, {1, 27, 5}}
};

static int rv1103_pwm_setup(FAR struct pwm_lowerhalf_s *dev)
{
  return 0;
}

static int rv1103_pwm_stop(FAR struct pwm_lowerhalf_s *dev)
{
  FAR struct rv1103_pwm_s *priv = (FAR struct rv1103_pwm_s *)dev;
  putreg32(getreg32(priv->base + PWM_CTRL) & ~PWM_CTRL_ENABLE,
           priv->base + PWM_CTRL);
  return 0;
}

static int rv1103_pwm_shutdown(FAR struct pwm_lowerhalf_s *dev)
{
  return rv1103_pwm_stop(dev);
}

#ifdef CONFIG_PWM_PULSECOUNT
static int rv1103_pwm_start(FAR struct pwm_lowerhalf_s *dev,
                            FAR const struct pwm_info_s *info,
                            FAR void *handle)
#else
static int rv1103_pwm_start(FAR struct pwm_lowerhalf_s *dev,
                            FAR const struct pwm_info_s *info)
#endif
{
  FAR struct rv1103_pwm_s *priv = (FAR struct rv1103_pwm_s *)dev;
  uint64_t duty;
  uint32_t period;
  uint32_t ctrl;

#ifdef CONFIG_PWM_PULSECOUNT
  if (info->count != 0)
    {
      return -ENOTSUP;
    }

  (void)handle;
#endif

  if (info->frequency == 0 || info->frequency > PWM_CLOCK)
    {
      return -ERANGE;
    }

  period = PWM_CLOCK / info->frequency;
  if (period < 2)
    {
      period = 2;
    }

#ifdef CONFIG_PWM_MULTICHAN
  duty = info->channels[0].duty;
#else
  duty = info->duty;
#endif
  duty = ((uint64_t)period * duty) >> 16;
  if (duty > period)
    {
      duty = period;
    }

  ctrl = getreg32(priv->base + PWM_CTRL) | PWM_CTRL_LOCK;
  putreg32(ctrl, priv->base + PWM_CTRL);
  putreg32(period, priv->base + PWM_PERIOD);
  putreg32((uint32_t)duty, priv->base + PWM_DUTY);
  ctrl &= ~PWM_CTRL_LOCK;
  ctrl |= PWM_CTRL_ENABLE;
  putreg32(ctrl, priv->base + PWM_CTRL);
  return 0;
}

static int rv1103_pwm_ioctl(FAR struct pwm_lowerhalf_s *dev, int cmd,
                            unsigned long arg)
{
  return -ENOTTY;
}

static const struct pwm_ops_s g_pwm_ops =
{
  .setup = rv1103_pwm_setup,
  .shutdown = rv1103_pwm_shutdown,
  .start = rv1103_pwm_start,
  .stop = rv1103_pwm_stop,
  .ioctl = rv1103_pwm_ioctl
};

static struct rv1103_pwm_s g_pwm =
{
  .ops = &g_pwm_ops
};

FAR struct pwm_lowerhalf_s *rv1103_pwminitialize(int channel)
{
  FAR const struct rv1103_pwm_pin_s *pin;
  unsigned int route = CONFIG_RV1103_PWM_ROUTE;
  uintptr_t blocks[3] = {0xff350000u, 0xff360000u, 0xff490000u};

  if (channel < 0 || channel > 11 || route > 2)
    {
      return NULL;
    }

  pin = &g_pwm_pins[channel][route];
  if (pin->bank == 0xff)
    {
      return NULL;
    }

  rv1103_periph_clock_pwm(channel / 4);
  rv1103_pinctrl_config(pin->bank, pin->pin, pin->function,
                        RV1103_PULL_NONE, false);
  g_pwm.base = blocks[channel / 4] + (channel & 3) * 0x10u;
  g_pwm.channel = channel;
  putreg32(0, g_pwm.base + PWM_CTRL);
  return (FAR struct pwm_lowerhalf_s *)&g_pwm;
}

#endif
