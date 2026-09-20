/****************************************************************************
 * vendor/rockchip/boards/rk3506/luckfox-lyra-zero-w/src/luckfox_sdmmc.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <syslog.h>

#include <nuttx/drivers/drivers.h>
#include <nuttx/fs/partition.h>
#include <nuttx/mmcsd.h>
#include <nuttx/sdio.h>

#include <arch/board/board.h>
#include <arch/chip/rk3506_sdmmc.h>

static void luckfox_sdmmc_register_raw(const char *blockdev,
                                       const char *chardev,
                                       int oflags)
{
  int ret;

  ret = bchdev_register(blockdev, chardev, oflags);
  if (ret < 0)
    {
      syslog(LOG_WARNING, "WARNING: Failed to create %s from %s: %d\n",
             chardev, blockdev, ret);
    }
}

static void luckfox_sdmmc_mount_data(void)
{
  int ret;

  ret = mkdir("/data", 0777);
  if (ret < 0 && errno != EEXIST)
    {
      syslog(LOG_WARNING, "WARNING: Failed to create /data: %d\n", errno);
      return;
    }

  ret = mount("/dev/rootfs", "/data", "vfat", 0, NULL);
  if (ret < 0)
    {
      syslog(LOG_INFO,
             "SDMMC: /data not mounted (%d); format /dev/rootfs once "
             "with mkfatfs\n",
             errno);
    }
  else
    {
      syslog(LOG_INFO, "SDMMC: /dev/rootfs mounted at /data as FAT\n");
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int luckfox_sdmmc_initialize(void)
{
  struct sdio_dev_s *sdio;
  int ret;

  sdio = rk3506_sdmmc_initialize(0);
  if (sdio == NULL)
    {
      syslog(LOG_ERR, "ERROR: Failed to initialize RK3506 SDMMC\n");
      return -ENODEV;
    }

  ret = mmcsd_slotinitialize(0, sdio);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to bind SDMMC slot 0: %d\n", ret);
      return ret;
    }

  ret = parse_block_partition("/dev/mmcsd0", NULL, NULL);
  if (ret < 0)
    {
      syslog(LOG_WARNING, "WARNING: Failed to parse SDMMC GPT: %d\n", ret);
    }
  else
    {
      luckfox_sdmmc_register_raw("/dev/uboot", "/dev/uboot.raw", O_RDONLY);
      luckfox_sdmmc_register_raw("/dev/boot", "/dev/boot.raw", O_RDONLY);
      luckfox_sdmmc_register_raw("/dev/amp", "/dev/amp.raw", O_RDONLY);
      luckfox_sdmmc_register_raw("/dev/rootfs", "/dev/rootfs.raw", O_RDWR);
      syslog(LOG_INFO,
             "SDMMC: GPT partitions registered; rootfs.raw is writable\n");
      luckfox_sdmmc_mount_data();
    }

  ret = bchdev_register("/dev/mmcsd0", "/dev/sdcard", O_RDONLY);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to create /dev/sdcard: %d\n", ret);
      return ret;
    }

  syslog(LOG_INFO,
         "SDMMC: /dev/mmcsd0 and read-only /dev/sdcard initialized\n");
  return 0;
}
