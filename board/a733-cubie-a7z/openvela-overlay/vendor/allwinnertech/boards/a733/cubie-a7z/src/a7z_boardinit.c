/****************************************************************************
 * vendor/allwinnertech/boards/a733/cubie-a7z/src/a7z_boardinit.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <spawn.h>
#include <sys/stat.h>
#include <syslog.h>

#include <nuttx/fs/fs.h>
#ifdef CONFIG_BOARD_A7Z_ST7735
#  include <nuttx/board.h>
#  include <nuttx/lcd/lcd_dev.h>
#endif

#include <arch/board/board.h>
#include <arch/chip/a733_peripherals.h>

#include "arm64_internal.h"

#if defined(CONFIG_UTILS_SSH) && !defined(CONFIG_SYSTEM_A733SERVICES)
static void a7z_sshd_autostart(void)
{
  struct stat info;
  pid_t pid;
  char *argv[] = {"sshd", "--service", NULL};
  int have_password;
  int have_keys;
  int ret;

  /* Authentication and the host key live on the writable data partition.
   * No credential is compiled into the image.  sshd binds before wlan0 has
   * an address and becomes reachable as soon as Wi-Fi completes DHCP.
   */

  mkdir("/data/ssh", S_IRWXU);
  have_password = stat("/data/ssh/sshd.auth", &info) == 0;
  have_keys = stat("/data/ssh/authorized_keys", &info) == 0;

  if (!have_password && !have_keys)
    {
      syslog(LOG_NOTICE,
             "A733: SSH not provisioned; run 'sshd --setup openvela' once\n");
      return;
    }

  ret = posix_spawn(&pid, "sshd", NULL, NULL, argv, NULL);
  if (ret == 0)
    {
      syslog(LOG_INFO,
             "A733: SSH task created pid=%d; awaiting SSHD listening\n", pid);
    }
  else
    {
      syslog(LOG_ERR, "A733: SSH service start failed (%d)\n", ret);
    }
}
#endif

void a733_board_initialize(void)
{
#ifdef CONFIG_ARCH_EARLY_PRINT
  /* Stable handoff marker.  UART0 is intentionally left in the state set by
   * the official U-Boot; clock and pinmux programming comes in a later stage.
   */

  arm64_lowputc('A');
  arm64_lowputc('7');
  arm64_lowputc('Z');
  arm64_lowputc('0');
  arm64_lowputc('\r');
  arm64_lowputc('\n');
#endif
}

int board_app_initialize(uintptr_t arg)
{
  int ret;

  (void)arg;

#ifdef CONFIG_FS_PROCFS
  ret = nx_mount(NULL, "/proc", "procfs", 0, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "A733: failed to mount procfs: %d\n", ret);
      return ret;
    }

  syslog(LOG_INFO, "A733: procfs mounted; 4 GiB split RAM enabled\n");
#else
  ret = OK;
#endif

#ifdef CONFIG_BOARD_A7Z_USER_LED
  ret = a7z_gpio_initialize();
  syslog(ret < 0 ? LOG_ERR : LOG_INFO,
         "A733: user LED %s (%d)\n", ret < 0 ? "failed" : "ready", ret);
#endif

#ifdef CONFIG_A733_WATCHDOG
  ret = a733_watchdog_initialize();
  syslog(ret < 0 ? LOG_ERR : LOG_INFO,
         "A733: watchdog %s (%d)\n", ret < 0 ? "failed" : "ready", ret);
#endif

#ifdef CONFIG_A733_TSADC
  ret = a733_tsadc_initialize();
  if (ret < 0)
    {
      syslog(LOG_WARNING, "A733: TSADC deferred (%d)\n", ret);
    }
#endif

#ifdef CONFIG_A733_HARDWARE_DIAG
  ret = a733_hwdiag_initialize();
  syslog(ret < 0 ? LOG_ERR : LOG_INFO,
         "A733: merged hardware diagnostics %s (%d)\n",
         ret < 0 ? "failed" : "ready", ret);
#endif

#ifdef CONFIG_A733_TRNG
  ret = a733_trng_initialize();
  syslog(ret < 0 ? LOG_ERR : LOG_INFO,
         "A733: hardware TRNG %s (%d)\n",
         ret < 0 ? "failed" : "ready", ret);
#endif

#ifdef CONFIG_A733_NPU_CHECKPOINT
  ret = a733_npu_initialize();
  syslog(ret < 0 ? LOG_ERR : LOG_INFO,
         "A733: VIP2 NPU hardware checkpoint %s (%d)\n",
         ret < 0 ? "failed" : "ready", ret);
#endif

#ifdef CONFIG_A733_UART4
  ret = a733_uart4_initialize();
  syslog(ret < 0 ? LOG_ERR : LOG_INFO,
         "A733: UART4 TW-TTS port %s (%d)\n",
         ret < 0 ? "failed" : "ready at /dev/ttyS4", ret);
#endif

#ifdef CONFIG_A733_I2S0_AUDIO
  ret = a733_i2s0_audio_initialize();
  syslog(ret < 0 ? LOG_ERR : LOG_INFO,
         "A733: I2S0 audio checkpoint %s (%d)\n",
         ret < 0 ? "failed" : "ready at /dev/a733-audio", ret);
#endif

#if defined(CONFIG_A733_HEADER_I2C) || defined(CONFIG_A733_HEADER_SPI) || \
    defined(CONFIG_A733_FAN_PWM)
  ret = a733_header_peripherals_initialize();
  syslog(ret < 0 ? LOG_ERR : LOG_INFO,
         "A733: header I2C/SPI and fan PWM %s (%d)\n",
         ret < 0 ? "failed" : "ready", ret);
#endif

#ifdef CONFIG_BOARD_A7Z_EARLY_ST7735
  /* Opt-in only.  board_lcd_getdev() runs the whole panel command table and a
   * 128x160 clear, which over the polling SPI1 lower-half is the slowest step
   * in bring-up.  Doing that here means an unattached or miswired panel stalls
   * the boot before NSH, with no console to explain it.  By default the panel is
   * registered on first use by the display app via a7z_lcd_ensure_registered().
   */

  ret = board_lcd_initialize();
  if (ret >= 0)
    {
      ret = lcddev_register(0);
    }

  syslog(ret < 0 ? LOG_ERR : LOG_INFO,
         "A733: ST7735 LCD %s (%d)\n",
         ret < 0 ? "failed" : "ready at /dev/lcd0", ret);
#endif

#ifdef CONFIG_A733_SDMMC0
  ret = a733_sdmmc0_initialize();
  syslog(ret < 0 ? LOG_ERR : LOG_INFO,
         "A733: SDMMC0 %s (%d)\n", ret < 0 ? "failed" : "ready", ret);

#  ifdef CONFIG_BOARD_A7Z_STORAGE
  if (ret >= 0)
    {
      ret = a7z_storage_initialize();
      syslog(ret < 0 ? LOG_ERR : LOG_INFO,
             "A733: GPT/FAT storage %s (%d)\n",
             ret < 0 ? "failed" : "ready", ret);
    }
#  endif
#endif

#ifdef CONFIG_A733_WIFI_USB_CHECKPOINT
  /* Official firmware is carried on the small openvela FAT data partition,
   * therefore initialize FCU760K only after SDMMC/GPT/FAT is available.
   */

  ret = a733_wifi_usb_initialize();
  syslog(ret < 0 ? LOG_ERR : LOG_INFO,
         "A733: onboard FCU760K USB/firmware checkpoint %s (%d)\n",
         ret < 0 ? "failed" : "ready", ret);
#endif

#ifdef CONFIG_A733_USB_CAMERA
  ret = a733_usb_camera_initialize();
  syslog(ret < 0 ? LOG_ERR : LOG_INFO,
         "A733: external USB0 UVC checkpoint %s (%d)\n",
         ret < 0 ? "failed" : "ready", ret);
#endif

#ifdef CONFIG_SYSTEM_A733SERVICES
  {
    pid_t pid;
    char *argv[] = {"service", "--boot", NULL};
    ret = posix_spawn(&pid, "service", NULL, NULL, argv, NULL);
    syslog(ret == 0 ? LOG_INFO : LOG_ERR,
           "A733: optional service startup dispatcher (%d)\n", ret);
  }
#elif defined(CONFIG_UTILS_SSH)
  a7z_sshd_autostart();
#endif

  return 0;
}
