/****************************************************************************
 * vendor/allwinnertech/chips/a733/a733_tsadc.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_A733_TSADC

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <syslog.h>

#include <nuttx/sensors/sensor.h>

#include "arm64_internal.h"

/* sun60iw2p1 THS uses the H616 register layout.  Its device-tree clock list
 * contains both the THS bus clock and the GPADC 24 MHz source.  These are
 * dedicated leaf gates, so enabling them does not change any shared PLL.
 */

#define A733_CCU_BASE            UINT64_C(0x02002000)
#define A733_GPADC0_24M_CLK      (A733_CCU_BASE + 0x0fc0)
#define A733_THS_BGR             (A733_CCU_BASE + 0x0fe4)
#define A733_GPADC0_24M_GATE     (1u << 31)
#define A733_THS_BUS_GATE        (1u << 0)
#define A733_THS_RESET_N         (1u << 16)

#define A733_THS_BASE            UINT64_C(0x02522000)
#define A733_THS_CTRL0           (A733_THS_BASE + 0x00)
#define A733_THS_ENABLE          (A733_THS_BASE + 0x04)
#define A733_THS_PC              (A733_THS_BASE + 0x08)
#define A733_THS_DATA_INTS       (A733_THS_BASE + 0x20)
#define A733_THS_MFC             (A733_THS_BASE + 0x30)
#define A733_THS_TEMP_DATA0      (A733_THS_BASE + 0xc0)
#define A733_THS_VALID_MASK      0x0fffu

struct a733_tsadc_s
{
  struct sensor_lowerhalf_s lower;
};

static int a733_tsadc_fetch(struct sensor_lowerhalf_s *lower,
                            struct file *filep, char *buffer,
                            size_t buflen);

static const struct sensor_ops_s g_a733_tsadc_ops =
{
  .fetch = a733_tsadc_fetch,
};

static struct a733_tsadc_s g_a733_tsadc =
{
  .lower =
    {
      .type = SENSOR_TYPE_TEMPERAUTRE,
      .nbuffer = 1,
      .ops = &g_a733_tsadc_ops,
    },
};

static void a733_tsadc_modifyreg(uint64_t address, uint32_t clearbits,
                                 uint32_t setbits)
{
  putreg32((getreg32(address) & ~clearbits) | setbits, address);
}

static int a733_tsadc_temperature(uint32_t code, int32_t *millicelsius)
{
  code &= A733_THS_VALID_MASK;
  if (code == 0 || code == A733_THS_VALID_MASK)
    {
      return -EAGAIN;
    }

  /* Exact piecewise coefficients from the official sun60iw2 BSP. */

  if (code > 1769)
    {
      *millicelsius = ((int32_t)code - 2822) * -62;
    }
  else
    {
      *millicelsius = ((int32_t)code - 2835) * -59;
    }

  return 0;
}

static int a733_tsadc_fetch(struct sensor_lowerhalf_s *lower,
                            struct file *filep, char *buffer,
                            size_t buflen)
{
  struct sensor_temp *sample;
  int32_t temp;
  int ret;

  (void)lower;
  (void)filep;

  if (buflen != sizeof(struct sensor_temp))
    {
      return -EINVAL;
    }

  ret = a733_tsadc_temperature(getreg32(A733_THS_TEMP_DATA0), &temp);
  if (ret < 0)
    {
      return ret;
    }

  sample = (struct sensor_temp *)buffer;
  sample->timestamp = sensor_get_timestamp();
  sample->temperature = (float)temp / 1000.0f;
  return sizeof(*sample);
}

int a733_tsadc_initialize(void)
{
  uint32_t code;
  int32_t temp;
  unsigned int retry;
  bool sample_ready;
  int ret;

  /* Follow the official DT clock/reset dependencies.  Reset first, enable
   * the dedicated 24 MHz leaf clock, then release THS reset and bus gate.
   */

  a733_tsadc_modifyreg(A733_THS_BGR,
                       A733_THS_RESET_N | A733_THS_BUS_GATE, 0);
  up_udelay(2);
  a733_tsadc_modifyreg(A733_GPADC0_24M_CLK, 0, A733_GPADC0_24M_GATE);
  a733_tsadc_modifyreg(A733_THS_BGR, 0,
                       A733_THS_RESET_N | A733_THS_BUS_GATE);
  up_udelay(10);

  /* Exact acquisition/filter/period sequence from
   * sun60iw2_thermal_init().  PC is offset 0x08 and TEMP_PERIOD occupies
   * bits 31:12; writing an unshifted value to offset 0x40 produces no data.
   */

  putreg32((47u << 16) | 479u, A733_THS_CTRL0);
  putreg32((1u << 2) | 1u, A733_THS_MFC);
  putreg32(28u << 12, A733_THS_PC);
  putreg32(0x1fu, A733_THS_DATA_INTS);
  putreg32(0x1fu, A733_THS_ENABLE);

  ret = -EAGAIN;
  code = 0;
  for (retry = 0; retry < 50; retry++)
    {
      up_udelay(1000);
      code = getreg32(A733_THS_TEMP_DATA0) & A733_THS_VALID_MASK;
      if ((getreg32(A733_THS_DATA_INTS) & 1u) != 0)
        {
          ret = a733_tsadc_temperature(code, &temp);
          if (ret == 0)
            {
              break;
            }
        }
    }

  sample_ready = ret == 0;
  if (!sample_ready)
    {
      syslog(LOG_INFO,
             "A733: THS first sample pending code=%lu ints=%08lx "
             "gpadc_clk=%08lx ths_bgr=%08lx; registering sensor\n",
             (unsigned long)code,
             (unsigned long)getreg32(A733_THS_DATA_INTS),
             (unsigned long)getreg32(A733_GPADC0_24M_CLK),
             (unsigned long)getreg32(A733_THS_BGR));
    }

  /* Registration must not depend on the first conversion completing during
   * board bring-up.  This matches the official Linux driver's behaviour:
   * the node exists immediately and fetch returns -EAGAIN until DATA_INTS
   * reports a valid sample.
   */

  ret = sensor_register(&g_a733_tsadc.lower, 0);
  if (ret == 0 && sample_ready)
    {
      syslog(LOG_INFO, "A733: temp0 code=%lu temp=%ld.%03ld C\n",
             (unsigned long)code, (long)(temp / 1000),
             (long)(temp < 0 ? -temp % 1000 : temp % 1000));
    }
  else if (ret == 0)
    {
      syslog(LOG_INFO, "A733: temp0 registered; conversion pending\n");
    }

  return ret;
}

#endif
