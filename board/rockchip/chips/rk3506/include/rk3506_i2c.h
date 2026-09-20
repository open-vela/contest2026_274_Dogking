/****************************************************************************
 * vendor/rockchip/chips/rk3506/include/rk3506_i2c.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __VENDOR_ROCKCHIP_CHIPS_RK3506_INCLUDE_RK3506_I2C_H
#define __VENDOR_ROCKCHIP_CHIPS_RK3506_INCLUDE_RK3506_I2C_H

#include <nuttx/config.h>
#include <nuttx/i2c/i2c_master.h>

#ifdef CONFIG_RK3506_I2C2
FAR struct i2c_master_s *rk3506_i2cbus_initialize(int bus);
#endif

#endif
