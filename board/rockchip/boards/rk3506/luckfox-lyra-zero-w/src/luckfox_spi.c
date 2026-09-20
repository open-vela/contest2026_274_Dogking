/****************************************************************************
 * vendor/rockchip/boards/rk3506/luckfox-lyra-zero-w/src/luckfox_spi.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_RK3506_SPI0

#include <errno.h>
#include <syslog.h>

#include <nuttx/spi/spi.h>
#include <nuttx/spi/spi_transfer.h>

#include <arch/chip/rk3506_spi.h>

int luckfox_spi_initialize(void)
{
  FAR struct spi_dev_s *spi;
  int ret;

  spi = rk3506_spibus_initialize(0);
  if (spi == NULL)
    {
      return -ENODEV;
    }

  ret = spi_register(spi, 0);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to register /dev/spi0: %d\n", ret);
      return ret;
    }

#ifdef CONFIG_RK3506_SPI0_LOOPBACK
  syslog(LOG_INFO,
         "SPI0: /dev/spi0 registered at GPIO0_C0..C3 (internal loopback)\n");
#else
  syslog(LOG_INFO, "SPI0: /dev/spi0 registered at GPIO0_C0..C3\n");
#endif
  return 0;
}

#endif
