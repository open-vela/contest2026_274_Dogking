/****************************************************************************
 * vendor/rockchip/chips/rk3506/include/rk3506_gpio.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __VENDOR_ROCKCHIP_CHIPS_RK3506_INCLUDE_RK3506_GPIO_H
#define __VENDOR_ROCKCHIP_CHIPS_RK3506_INCLUDE_RK3506_GPIO_H

#include <stdbool.h>

/* GPIO1 is in the RK3506 core power domain.  These helpers provide the
 * first, deliberately small GPIO slice needed by both Lyra boards.  Other
 * banks will be added with their own clock-domain handling.
 */

int rk3506_gpio1_config_output(unsigned int pin, bool initial);
bool rk3506_gpio1_read(unsigned int pin);
void rk3506_gpio1_write(unsigned int pin, bool value);

#endif /* __VENDOR_ROCKCHIP_CHIPS_RK3506_INCLUDE_RK3506_GPIO_H */
