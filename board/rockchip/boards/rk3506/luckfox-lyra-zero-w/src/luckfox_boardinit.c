/****************************************************************************
 * vendor/rockchip/boards/rk3506/luckfox-lyra-zero-w/src/luckfox_boardinit.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <errno.h>
#include <syslog.h>

#include <nuttx/fs/fs.h>
#include <nuttx/kthread.h>
#include <nuttx/signal.h>

#include <arch/board/board.h>

#ifdef CONFIG_RK3506_SDMMC
static int luckfox_sdmmc_worker(int argc, char *argv[])
{
  int ret;

  (void)argc;
  (void)argv;

  /* Let NSH become interactive before touching SDMMC.  Keeping card
   * discovery out of board_late_initialize also prevents a failed probe
   * from blocking the system's init task.
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

#ifdef CONFIG_RK3506_AIC8800DC_PROBE
static int luckfox_wifi_probe_worker(int argc, char *argv[])
{
  int ret;

  (void)argc;
  (void)argv;

  /* Run outside the init task.  Besides keeping NSH responsive, this gives
   * the board rails time to settle before the module is power-cycled.
   */

  nxsig_usleep(1000 * 1000);
  ret = luckfox_wifi_probe_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: AIC8800DC hardware probe failed: %d\n", ret);
    }

  return 0;
}
#endif

int board_app_initialize(uintptr_t arg)
{
  /* UART0, clocks and DDR were initialized by U-Boot.  Device bring-up will
   * be added here as the RK3506 port grows.
   */

  (void)arg;
  return 0;
}

#ifdef CONFIG_BOARD_LATE_INITIALIZE
void board_late_initialize(void)
{
#if defined(CONFIG_FS_PROCFS) || defined(CONFIG_DEV_GPIO) || \
    defined(CONFIG_RK3506_SDMMC) || defined(CONFIG_RK3506_I2C2) || \
    defined(CONFIG_RK3506_SPI0) || \
    defined(CONFIG_RK3506_AIC8800DC_PROBE)
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

#ifdef CONFIG_RK3506_I2C2
  ret = luckfox_i2c_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to initialize I2C2: %d\n", ret);
    }
#endif

#ifdef CONFIG_RK3506_SPI0
  ret = luckfox_spi_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to initialize SPI0: %d\n", ret);
    }
#endif

#ifdef CONFIG_RK3506_SDMMC
  ret = kthread_create("sdmmc-init", 50, 4096,
                       luckfox_sdmmc_worker, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to create SDMMC worker: %d\n", ret);
    }
#endif

#ifdef CONFIG_RK3506_AIC8800DC_PROBE
  ret = kthread_create("wifi-probe", 50, 4096,
                       luckfox_wifi_probe_worker, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to create Wi-Fi probe worker: %d\n",
             ret);
    }
#endif
}
#endif
