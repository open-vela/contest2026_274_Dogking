/****************************************************************************
 * vendor/rockchip/chips/rv1103/rv1103_tsadc.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_RV1103_TSADC

#include <errno.h>
#include <stdint.h>
#include <syslog.h>

#include <nuttx/sensors/sensor.h>

#include <arch/chip/rv1103_tsadc.h>

#include "arm_internal.h"

/* This is the RV1106 TSADC v9 block used by RV1103.  Addresses, clock
 * topology, power sequence and conversion data are taken from Rockchip's
 * rv1106.dtsi, clk-rv1106.c and rockchip_thermal.c.
 */

#define RV1103_TSADC_BASE                0xff3c8000u
#define RV1103_TSADC_AUTO_CON            (RV1103_TSADC_BASE + 0x04u)
#define RV1103_TSADC_AUTO_SRC_CON        (RV1103_TSADC_BASE + 0x0cu)
#define RV1103_TSADC_INT_PD              (RV1103_TSADC_BASE + 0x24u)
#define RV1103_TSADC_DATA0               (RV1103_TSADC_BASE + 0x2cu)
#define RV1103_TSADC_INT_DEBOUNCE        (RV1103_TSADC_BASE + 0x14cu)
#define RV1103_TSADC_TSHUT_DEBOUNCE      (RV1103_TSADC_BASE + 0x150u)
#define RV1103_TSADC_AUTO_PERIOD         (RV1103_TSADC_BASE + 0x154u)
#define RV1103_TSADC_AUTO_PERIOD_HT      (RV1103_TSADC_BASE + 0x158u)
#define RV1103_TSADC_Q_MAX               (RV1103_TSADC_BASE + 0x210u)
#define RV1103_TSADC_FLOW_CON            (RV1103_TSADC_BASE + 0x218u)

#define RV1103_GRF_TSADC_CON             0xff06000cu
#define RV1103_VO_CLKSEL_CON3            0xff3bc30cu
#define RV1103_VO_CLKGATE_CON2           0xff3bc808u
#define RV1103_VO_SOFTRST_CON2           0xff3bca08u

#define RV1103_TSADC_AUTO_EN             (1u << 0)
#define RV1103_TSADC_AUTO_EN_MASK        (1u << 16)
#define RV1103_TSADC_AUTO_Q_SEL_EN       (1u << 1)
#define RV1103_TSADC_AUTO_Q_SEL_MASK     (1u << 17)
#define RV1103_TSADC_CH0_EN              (1u << 0)
#define RV1103_TSADC_CH0_EN_MASK         (1u << 16)
#define RV1103_TSADC_DATA_MASK           0x0fffu

struct rv1103_tsadc_point_s
{
  uint16_t code;
  int32_t millicelsius;
};

struct rv1103_tsadc_dev_s
{
  struct sensor_lowerhalf_s lower;
};

static int rv1103_tsadc_fetch(struct sensor_lowerhalf_s *lower,
                              struct file *filep, char *buffer,
                              size_t buflen);

static const struct sensor_ops_s g_rv1103_tsadc_ops =
{
  .fetch = rv1103_tsadc_fetch,
};

static struct rv1103_tsadc_dev_s g_rv1103_tsadc =
{
  .lower =
    {
      /* NuttX keeps the historical TEMPERAUTRE spelling for type 7.  This
       * is die temperature, not ambient temperature, and maps to temp0.
       */

      .type = SENSOR_TYPE_TEMPERAUTRE,
      .nbuffer = 1,
      .ops = &g_rv1103_tsadc_ops,
    },
};

/* Official RV1106 code table.  The end points are retained to match the
 * Linux driver's interpolation behavior.
 */

static const struct rv1103_tsadc_point_s g_rv1103_tsadc_table[] =
{
  {363,  -60000},
  {396,  -40000},
  {504,   25000},
  {605,   85000},
  {673,  125000},
  {758,  180000},
  {4095, 180000},
};

static int rv1103_tsadc_code_to_temp(uint32_t code, int32_t *temperature)
{
  unsigned int i;
  int64_t delta_temp;
  uint32_t delta_code;

  code &= RV1103_TSADC_DATA_MASK;
  if (code < g_rv1103_tsadc_table[0].code)
    {
      return -EAGAIN;
    }

  for (i = 1; i < sizeof(g_rv1103_tsadc_table) /
                      sizeof(g_rv1103_tsadc_table[0]); i++)
    {
      if (code <= g_rv1103_tsadc_table[i].code)
        {
          delta_temp = g_rv1103_tsadc_table[i].millicelsius -
                       g_rv1103_tsadc_table[i - 1].millicelsius;
          delta_code = g_rv1103_tsadc_table[i].code -
                       g_rv1103_tsadc_table[i - 1].code;
          *temperature = g_rv1103_tsadc_table[i - 1].millicelsius +
                         (int32_t)(delta_temp *
                         (code - g_rv1103_tsadc_table[i - 1].code) /
                         delta_code);
          return 0;
        }
    }

  return -ERANGE;
}

static int rv1103_tsadc_fetch(struct sensor_lowerhalf_s *lower,
                              struct file *filep, char *buffer,
                              size_t buflen)
{
  struct sensor_temp *sample;
  int32_t millicelsius;
  uint32_t code;
  int ret;

  (void)lower;
  (void)filep;

  if (buflen != sizeof(struct sensor_temp))
    {
      return -EINVAL;
    }

  code = getreg32(RV1103_TSADC_DATA0) & RV1103_TSADC_DATA_MASK;
  ret = rv1103_tsadc_code_to_temp(code, &millicelsius);
  if (ret < 0)
    {
      return ret;
    }

  sample = (struct sensor_temp *)buffer;
  sample->timestamp = sensor_get_timestamp();
  sample->temperature = (float)millicelsius / 1000.0f;
  return sizeof(struct sensor_temp);
}

static void rv1103_tsadc_enable_clocks(void)
{
  /* xin24m / 24 = 1 MHz TSADC, xin24m / 2 = 12 MHz TSEN. */

  putreg32((0x03ffu << 16) | 0x0037u, RV1103_VO_CLKSEL_CON3);

  /* Rockchip gate bits are active high: writing zero ungates all three. */

  putreg32(0x0007u << 16, RV1103_VO_CLKGATE_CON2);
}

static void rv1103_tsadc_reset(void)
{
  /* VOSOFTRST_CON02 bits 0 and 1 are P_TSADC and TSADC. */

  putreg32((0x0003u << 16) | 0x0003u, RV1103_VO_SOFTRST_CON2);
  up_udelay(10);
  putreg32(0x0003u << 16, RV1103_VO_SOFTRST_CON2);
}

static void rv1103_tsadc_power_up(void)
{
  /* Enable TSEN first, then the analog block, exactly as the BSP does. */

  putreg32(0x01000100u, RV1103_GRF_TSADC_CON);
  up_udelay(10);
  putreg32(0x00ff0000u | 0x0007u, RV1103_GRF_TSADC_CON);
  up_udelay(100);
}

int rv1103_tsadc_initialize(void)
{
  unsigned int retry;
  uint32_t code;
  int32_t millicelsius;
  int ret;

  rv1103_tsadc_enable_clocks();

  /* Stop automatic conversion before resetting and reconfiguring. */

  putreg32(RV1103_TSADC_AUTO_EN_MASK, RV1103_TSADC_AUTO_CON);
  rv1103_tsadc_reset();
  rv1103_tsadc_power_up();

  putreg32(250, RV1103_TSADC_AUTO_PERIOD);
  putreg32(250, RV1103_TSADC_AUTO_PERIOD_HT);
  putreg32(4, RV1103_TSADC_INT_DEBOUNCE);
  putreg32(4, RV1103_TSADC_TSHUT_DEBOUNCE);
  putreg32(0x00010001u, RV1103_TSADC_INT_PD);
  putreg32(0x00100010u, RV1103_TSADC_FLOW_CON);
  putreg32(0xffff0400u, RV1103_TSADC_Q_MAX);

  /* Keep TSHUT low-active but do not enable either shutdown route. */

  putreg32(1u << 24, RV1103_TSADC_AUTO_CON);
  putreg32(RV1103_TSADC_AUTO_Q_SEL_EN |
           RV1103_TSADC_AUTO_Q_SEL_MASK, RV1103_TSADC_AUTO_CON);
  putreg32(RV1103_TSADC_CH0_EN | RV1103_TSADC_CH0_EN_MASK,
           RV1103_TSADC_AUTO_SRC_CON);
  putreg32(RV1103_TSADC_AUTO_EN | RV1103_TSADC_AUTO_EN_MASK,
           RV1103_TSADC_AUTO_CON);

  ret = -EAGAIN;
  code = 0;
  for (retry = 0; retry < 20 && ret < 0; retry++)
    {
      up_udelay(1000);
      code = getreg32(RV1103_TSADC_DATA0) & RV1103_TSADC_DATA_MASK;
      ret = rv1103_tsadc_code_to_temp(code, &millicelsius);
    }

  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: TSADC invalid initial code=%lu: %d\n",
             (unsigned long)code, ret);
      return ret;
    }

  ret = sensor_register(&g_rv1103_tsadc.lower, 0);
  if (ret < 0)
    {
      return ret;
    }

  syslog(LOG_INFO, "TSADC: /dev/uorb/sensor_temp0 code=%lu temp=%ld.%03ld C\n",
         (unsigned long)code, (long)(millicelsius / 1000),
         (long)(millicelsius < 0 ? -millicelsius % 1000 :
                                  millicelsius % 1000));
  return 0;
}

#endif /* CONFIG_RV1103_TSADC */
