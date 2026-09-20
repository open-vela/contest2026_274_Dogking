/****************************************************************************
 * vendor/rockchip/chips/rk3506/include/rk3506_spi.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __VENDOR_ROCKCHIP_CHIPS_RK3506_INCLUDE_RK3506_SPI_H
#define __VENDOR_ROCKCHIP_CHIPS_RK3506_INCLUDE_RK3506_SPI_H

#include <nuttx/config.h>
#include <nuttx/spi/spi.h>

#ifdef CONFIG_RK3506_SPI0
FAR struct spi_dev_s *rk3506_spibus_initialize(int bus);
#endif

#endif
