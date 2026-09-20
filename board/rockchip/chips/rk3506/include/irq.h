/****************************************************************************
 * vendor/rockchip/chips/rk3506/include/irq.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __VENDOR_ROCKCHIP_CHIPS_RK3506_INCLUDE_IRQ_H
#define __VENDOR_ROCKCHIP_CHIPS_RK3506_INCLUDE_IRQ_H

/* GIC interrupt IDs from Rockchip's RK3506 CMSIS device description. */

#define RK3506_IRQ_CNTPNS     30
#define RK3506_IRQ_UART0      66
#define RK3506_IRQ_SDMMC      118

/* RK3506 implements interrupt IDs 0 through 191. */

#define NR_IRQS               192

#endif /* __VENDOR_ROCKCHIP_CHIPS_RK3506_INCLUDE_IRQ_H */
