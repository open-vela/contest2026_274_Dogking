/****************************************************************************
 * vendor/rockchip/boards/rv1103/luckfox-pico-mini/src/luckfox_peripherals.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>
#include <errno.h>
#include <stdio.h>
#include <syslog.h>
#include <nuttx/i2c/i2c_master.h>
#include <nuttx/spi/spi.h>
#include <nuttx/spi/spi_transfer.h>
#include <nuttx/timers/pwm.h>
#include <arch/chip/rv1103_periph.h>

int luckfox_peripherals_initialize(void)
{
  int ret = 0;

#ifdef CONFIG_RV1103_I2C
  FAR struct i2c_master_s *i2c;

  i2c = rv1103_i2cbus_initialize(3);
  if (i2c == NULL || (ret = i2c_register(i2c, 3)) < 0)
    {
      syslog(LOG_ERR, "ERROR: I2C3 registration failed: %d\n", ret);
      return ret < 0 ? ret : -ENODEV;
    }

  i2c = rv1103_i2cbus_initialize(4);
  if (i2c == NULL || (ret = i2c_register(i2c, 4)) < 0)
    {
      syslog(LOG_ERR, "ERROR: I2C4 registration failed: %d\n", ret);
      return ret < 0 ? ret : -ENODEV;
    }

  syslog(LOG_INFO,
         "I2C: /dev/i2c3 (M1) and /dev/i2c4 (M2) registered\n");
#endif

#ifdef CONFIG_RV1103_SPI0
  FAR struct spi_dev_s *spi = rv1103_spibus_initialize(0);
  if (spi == NULL || (ret = spi_register(spi, 0)) < 0)
    {
      syslog(LOG_ERR, "ERROR: SPI0 registration failed: %d\n", ret);
      return ret < 0 ? ret : -ENODEV;
    }

  syslog(LOG_INFO, "SPI: /dev/spi0 registered on SPI0_M0\n");
#endif

#ifdef CONFIG_RV1103_SPI1
  {
    FAR struct spi_dev_s *spi1 = rv1103_spibus_initialize(1);
    if (spi1 == NULL || (ret = spi_register(spi1, 1)) < 0)
      {
        syslog(LOG_ERR, "ERROR: SPI1 registration failed: %d\n", ret);
        return ret < 0 ? ret : -ENODEV;
      }

    syslog(LOG_INFO, "SPI: /dev/spi1 registered on SPI1_M0\n");
  }
#endif

#ifdef CONFIG_RV1103_PWM
  {
    FAR struct pwm_lowerhalf_s *pwm;

    /* Reject known collisions with the always-on default set. */

    if ((CONFIG_RV1103_PWM_CHANNEL == 0 &&
         CONFIG_RV1103_PWM_ROUTE == 1) ||
        (CONFIG_RV1103_PWM_CHANNEL == 2 &&
         CONFIG_RV1103_PWM_ROUTE == 2) ||
        (CONFIG_RV1103_PWM_CHANNEL == 3 &&
         CONFIG_RV1103_PWM_ROUTE == 2) ||
        (CONFIG_RV1103_PWM_CHANNEL >= 4 &&
         CONFIG_RV1103_PWM_CHANNEL <= 6 &&
         CONFIG_RV1103_PWM_ROUTE == 2) ||
        (CONFIG_RV1103_PWM_CHANNEL == 7 &&
         CONFIG_RV1103_PWM_ROUTE == 2) ||
        (CONFIG_RV1103_PWM_CHANNEL == 8 &&
         CONFIG_RV1103_PWM_ROUTE == 1) ||
        (CONFIG_RV1103_PWM_CHANNEL == 9 &&
         CONFIG_RV1103_PWM_ROUTE == 1) ||
        (CONFIG_RV1103_PWM_CHANNEL == 10 &&
         CONFIG_RV1103_PWM_ROUTE == 2) ||
        (CONFIG_RV1103_PWM_CHANNEL == 11 &&
         CONFIG_RV1103_PWM_ROUTE == 2))
      {
        syslog(LOG_ERR,
               "ERROR: PWM%d_M%d conflicts with default I2C/SPI/UART set\n",
               CONFIG_RV1103_PWM_CHANNEL, CONFIG_RV1103_PWM_ROUTE);
        return -EBUSY;
      }

    pwm = rv1103_pwminitialize(CONFIG_RV1103_PWM_CHANNEL);
    if (pwm == NULL || (ret = pwm_register("/dev/pwm0", pwm)) < 0)
      {
        return ret < 0 ? ret : -ENODEV;
      }

    syslog(LOG_INFO, "PWM: /dev/pwm0 = PWM%d_M%d\n",
           CONFIG_RV1103_PWM_CHANNEL, CONFIG_RV1103_PWM_ROUTE);
  }
#endif

#ifdef CONFIG_RV1103_SARADC
  ret = rv1103_saradc_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: SARADC registration failed: %d\n", ret);
      return ret;
    }

  syslog(LOG_INFO,
         "SARADC: /dev/adc0 and polled /dev/buttons registered\n");
#endif

  return 0;
}
