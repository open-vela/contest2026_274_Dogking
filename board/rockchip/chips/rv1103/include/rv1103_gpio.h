/****************************************************************************
 * vendor/rockchip/chips/rv1103/include/rv1103_gpio.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __VENDOR_ROCKCHIP_CHIPS_RV1103_INCLUDE_RV1103_GPIO_H
#define __VENDOR_ROCKCHIP_CHIPS_RV1103_INCLUDE_RV1103_GPIO_H

#include <stdbool.h>

/* This deliberately small first slice covers GPIO3, which contains the
 * Luckfox Pico Mini work LED.  Other banks live in different clock domains
 * and will be added together with the peripherals that need them.
 */

int rv1103_gpio3_config_output(unsigned int pin, bool initial);
bool rv1103_gpio3_read(unsigned int pin);
void rv1103_gpio3_write(unsigned int pin, bool value);

#endif /* __VENDOR_ROCKCHIP_CHIPS_RV1103_INCLUDE_RV1103_GPIO_H */
