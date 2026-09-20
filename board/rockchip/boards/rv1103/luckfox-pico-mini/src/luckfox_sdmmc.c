/****************************************************************************
 * vendor/rockchip/boards/rv1103/luckfox-pico-mini/src/luckfox_sdmmc.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <errno.h>
#include <syslog.h>

#include <nuttx/mmcsd.h>
#include <nuttx/sdio.h>

#include <arch/chip/rv1103_sdmmc.h>

int luckfox_sdmmc_initialize(void)
{
  FAR struct sdio_dev_s *sdio;
  bool present;
  int ret;

  sdio = rv1103_sdmmc_initialize(0);
  if (sdio == NULL)
    {
      syslog(LOG_ERR, "ERROR: RV1103 SDMMC controller initialization failed\n");
      return -ENODEV;
    }

  ret = mmcsd_slotinitialize(0, sdio);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to bind SDMMC slot 0: %d\n", ret);
      return ret;
    }

  present = (SDIO_STATUS(sdio) & SDIO_STATUS_PRESENT) != 0;
  syslog(LOG_INFO,
         "SDMMC: controller ready at 0xffaa0000, card %s\n",
         present ? "present; enumeration requested" : "not present");
  return 0;
}
