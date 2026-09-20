/****************************************************************************
 * vendor/rockchip/boards/rk3506/luckfox-lyra-zero-w/src/luckfox_i2c.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_RK3506_I2C2

#include <errno.h>
#include <syslog.h>

#include <nuttx/i2c/i2c_master.h>

#include <arch/chip/rk3506_i2c.h>

int luckfox_i2c_initialize(void)
{
  FAR struct i2c_master_s *i2c;
  int ret;

  i2c = rk3506_i2cbus_initialize(2);
  if (i2c == NULL)
    {
      return -ENODEV;
    }

  ret = i2c_register(i2c, 2);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to register /dev/i2c2: %d\n", ret);
      return ret;
    }

  syslog(LOG_INFO,
         "I2C2: /dev/i2c2 registered at 100 kHz on GPIO0_A0/A1 RMIO\n");
  return 0;
}

#endif
