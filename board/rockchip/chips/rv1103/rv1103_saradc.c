/****************************************************************************
 * vendor/rockchip/chips/rv1103/rv1103_saradc.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_RV1103_SARADC

#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <nuttx/arch.h>
#include <nuttx/analog/adc.h>
#include <nuttx/analog/ioctl.h>
#include <nuttx/input/buttons.h>
#include <arch/chip/rv1103_periph.h>

#include "arm_internal.h"

#define SARADC_BASE       0xff3c0000u
#define SARADC_CONV_CON   (SARADC_BASE + 0x000u)
#define SARADC_T_PD_SOC   (SARADC_BASE + 0x004u)
#define SARADC_T_DAS_SOC  (SARADC_BASE + 0x00cu)
#define SARADC_END_INT_EN (SARADC_BASE + 0x104u)
#define SARADC_END_INT_ST (SARADC_BASE + 0x110u)
#define SARADC_DATA(ch)   (SARADC_BASE + 0x120u + 4u * (ch))
#define SARADC_START      (1u << 4)
#define SARADC_SINGLE_PD  (1u << 5)

struct rv1103_adc_s
{
  FAR const struct adc_callback_s *cb;
};

static struct rv1103_adc_s g_adcpriv;
static int rv1103_adc_bind(FAR struct adc_dev_s *dev,
                           FAR const struct adc_callback_s *cb)
{
  g_adcpriv.cb = cb;
  return 0;
}

static void rv1103_adc_reset(FAR struct adc_dev_s *dev)
{
  putreg32(1, SARADC_END_INT_ST);
}

static int rv1103_adc_setup(FAR struct adc_dev_s *dev)
{
  return 0;
}

static void rv1103_adc_shutdown(FAR struct adc_dev_s *dev)
{
}

static void rv1103_adc_rxint(FAR struct adc_dev_s *dev, bool enable)
{
}

uint16_t rv1103_saradc_read(unsigned int channel)
{
  unsigned int timeout;

  if (channel > 1)
    {
      return 0xffff;
    }

  putreg32(0x0c, SARADC_T_DAS_SOC);
  putreg32(0x20, SARADC_T_PD_SOC);
  putreg32(0x00010001, SARADC_END_INT_EN);
  putreg32((0x3fu << 16) | SARADC_START | SARADC_SINGLE_PD | channel,
           SARADC_CONV_CON);
  for (timeout = 0; timeout < 2000; timeout++)
    {
      if ((getreg32(SARADC_END_INT_ST) & 1u) != 0)
        {
          uint16_t value = getreg32(SARADC_DATA(channel)) & 0xfff;
          putreg32(1, SARADC_END_INT_ST);
          return value;
        }

      up_udelay(2);
    }

  return 0xffff;
}

static int rv1103_adc_ioctl(FAR struct adc_dev_s *dev, int cmd,
                            unsigned long arg)
{
  uint16_t sample;

  if (cmd != ANIOC_TRIGGER)
    {
      return -ENOTTY;
    }

  sample = rv1103_saradc_read(0);
  if (sample == 0xffff)
    {
      return -ETIMEDOUT;
    }

  return g_adcpriv.cb == NULL ? 0 :
         g_adcpriv.cb->au_receive(dev, 0, sample);
}

static const struct adc_ops_s g_adcops =
{
  .ao_bind = rv1103_adc_bind,
  .ao_reset = rv1103_adc_reset,
  .ao_setup = rv1103_adc_setup,
  .ao_shutdown = rv1103_adc_shutdown,
  .ao_rxint = rv1103_adc_rxint,
  .ao_ioctl = rv1103_adc_ioctl
};

static struct adc_dev_s g_adcdev =
{
  .ad_ops = &g_adcops,
  .ad_priv = &g_adcpriv
};

#ifdef CONFIG_RV1103_ADC_KEYS
static btn_buttonset_t rv1103_btn_supported(
  FAR const struct btn_lowerhalf_s *lower)
{
  return 3;
}

static btn_buttonset_t rv1103_btn_buttons(
  FAR const struct btn_lowerhalf_s *lower)
{
  uint16_t raw = rv1103_saradc_read(0);
  uint32_t uv;

  if (raw == 0xffff)
    {
      return 0;
    }

  uv = (uint32_t)raw * 1800000u / 4095u;
  if (uv < 200390u)
    {
      return 1u; /* volume up, nominal 0 uV */
    }
  else if (uv < 1100390u)
    {
      return 2u; /* volume down, nominal 400781 uV */
    }

  return 0;
}

static void rv1103_btn_enable(FAR const struct btn_lowerhalf_s *lower,
                              btn_buttonset_t press,
                              btn_buttonset_t release,
                              btn_handler_t handler, FAR void *arg)
{
  /* adc-keys is polled, matching the official Linux 100 ms policy. */
}

static const struct btn_lowerhalf_s g_btnlower =
{
  .bl_supported = rv1103_btn_supported,
  .bl_buttons = rv1103_btn_buttons,
  .bl_enable = rv1103_btn_enable,
  .bl_write = NULL
};
#endif

int rv1103_saradc_initialize(void)
{
  int ret;

  rv1103_periph_clock_saradc();
  putreg32(1, SARADC_END_INT_ST);
  ret = adc_register("/dev/adc0", &g_adcdev);
  if (ret < 0)
    {
      return ret;
    }

#ifdef CONFIG_RV1103_ADC_KEYS
  ret = btn_register("/dev/buttons", &g_btnlower);
#endif
  return ret;
}

#endif
