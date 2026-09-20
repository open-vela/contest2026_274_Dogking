/****************************************************************************
 * vendor/rockchip/boards/rv1103/luckfox-pico-mini/src/luckfox_boardinit.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdint.h>
#include <syslog.h>

#include <nuttx/fs/fs.h>
#include <nuttx/kthread.h>
#include <nuttx/signal.h>

#include <arch/board/board.h>
#include <arch/chip/rv1103_wdt.h>
#include <arch/chip/rv1103_tsadc.h>
#include <arch/chip/rv1103_dma.h>
#include <arch/chip/rv1103_sfc_nand.h>
#include <arch/chip/rv1103_usb.h>

#ifdef CONFIG_RV1103_SDMMC
static int luckfox_sdmmc_worker(int argc, FAR char *argv[])
{
  int ret;

  (void)argc;
  (void)argv;

  /* Enumeration can wait for card protocol timeouts.  Keep it outside the
   * init task so a missing or marginal card never delays NSH.
   */

  nxsig_usleep(3 * 1000 * 1000);
  ret = luckfox_sdmmc_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to initialize SDMMC: %d\n", ret);
    }

  return 0;
}
#endif

int board_app_initialize(uintptr_t arg)
{
  (void)arg;
  return 0;
}

#ifdef CONFIG_BOARD_LATE_INITIALIZE
void board_late_initialize(void)
{
#if defined(CONFIG_FS_PROCFS) || defined(CONFIG_DEV_GPIO) || \
    defined(CONFIG_RV1103_WATCHDOG) || defined(CONFIG_RV1103_TSADC) || \
    defined(CONFIG_RV1103_PL330_DMA) || defined(CONFIG_RV1103_SFC_NAND) || \
    defined(CONFIG_RV1103_I2C) || defined(CONFIG_RV1103_SPI) || \
    defined(CONFIG_RV1103_SC3336_CHECKPOINT) || \
    defined(CONFIG_RV1103_PWM) || defined(CONFIG_RV1103_SARADC) || \
    defined(CONFIG_RV1103_SDMMC) || \
    defined(CONFIG_RV1103_USB_DWC3_CHECKPOINT) || \
    defined(CONFIG_RV1103_USB_CDCACM)
  int ret;
#endif

#ifdef CONFIG_FS_PROCFS
  ret = nx_mount(NULL, "/proc", "procfs", 0, NULL);

  if (ret < 0 && ret != -EBUSY)
    {
      syslog(LOG_ERR, "ERROR: Failed to mount procfs: %d\n", ret);
    }
#endif

#ifdef CONFIG_DEV_GPIO
  ret = luckfox_gpio_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to initialize GPIO: %d\n", ret);
    }
#endif

#if defined(CONFIG_RV1103_I2C) || defined(CONFIG_RV1103_SPI) || \
    defined(CONFIG_RV1103_PWM) || defined(CONFIG_RV1103_SARADC)
  ret = luckfox_peripherals_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to initialize board peripherals: %d\n",
             ret);
    }
#endif

#ifdef CONFIG_RV1103_SC3336_CHECKPOINT
  ret = luckfox_sc3336_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: SC3336 checkpoint failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_RV1103_WATCHDOG
  ret = rv1103_wdt_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to initialize watchdog: %d\n", ret);
    }
#endif

#ifdef CONFIG_RV1103_TSADC
  ret = rv1103_tsadc_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to initialize TSADC: %d\n", ret);
    }
#endif

#ifdef CONFIG_RV1103_PL330_DMA
  ret = rv1103_dma_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to initialize PL330 DMA: %d\n", ret);
    }
#endif

#ifdef CONFIG_RV1103_SFC_NAND
  ret = rv1103_sfc_nand_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to initialize SFC NAND: %d\n", ret);
    }
#ifdef CONFIG_FS_LITTLEFS
  else
    {
      /* Mount LittleFS directly on the only writable NAND partition.  The
       * autoformat option is intentionally limited to userdata by the MTD
       * partition node and by the physical-range checks in the controller
       * driver.  A mount failure must not prevent NSH from starting.
       */

      ret = nx_mount("/dev/nand-userdata", "/data", "littlefs", 0,
                     "autoformat");
      if (ret < 0 && ret != -EBUSY)
        {
          syslog(LOG_ERR,
                 "ERROR: Failed to mount LittleFS userdata at /data: %d\n",
                 ret);
        }
      else
        {
          syslog(LOG_INFO,
                 "SFC NAND: /dev/nand-userdata mounted at /data as LittleFS\n");
        }
    }
#endif
#endif

#ifdef CONFIG_RV1103_USB_DWC3_CHECKPOINT
  ret = rv1103_usb_dwc3_checkpoint();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: USB DWC3 checkpoint failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_RV1103_USB_CDCACM
  if (ret >= 0)
    {
      ret = rv1103_usb_cdcacm_initialize();
      if (ret < 0)
        {
          syslog(LOG_ERR, "ERROR: USB CDC ACM initialization failed: %d\n",
                 ret);
        }
    }
#endif

#ifdef CONFIG_RV1103_SDMMC
  ret = kthread_create("sdmmc-init", 50, 4096,
                       luckfox_sdmmc_worker, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to create SDMMC worker: %d\n", ret);
    }
#endif
}
#endif
