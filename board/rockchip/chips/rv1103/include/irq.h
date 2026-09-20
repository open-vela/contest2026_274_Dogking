/****************************************************************************
 * vendor/rockchip/chips/rv1103/include/irq.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __VENDOR_ROCKCHIP_CHIPS_RV1103_INCLUDE_IRQ_H
#define __VENDOR_ROCKCHIP_CHIPS_RV1103_INCLUDE_IRQ_H

/* ARM architectural timer PPI and RV1103 UART2 SPI. */

#define RV1103_IRQ_CNTPS      29
#define RV1103_IRQ_CNTPNS     30
#define RV1103_IRQ_UART2      59
#define RV1103_IRQ_SDMMC      84 /* DTS GIC_SPI 52 + architectural base 32 */
#define RV1103_IRQ_RKCIF     138 /* DTS GIC_SPI 106 + architectural base 32 */

/* Vendor U-Boot enters the RV1103 payload without switching the Cortex-A7
 * out of Secure state.  CNTP therefore asserts the secure physical-timer
 * PPI (29), even though NuttX deliberately keeps its proven non-TrustZone
 * GIC/exception model for this standalone port.
 */

#define ARM_ARCH_TIMER_IRQ              RV1103_IRQ_CNTPS
#define ARM_ARCH_TIMER_SECONDARY_IRQ    RV1103_IRQ_CNTPNS

/* Linux registers both physical-timer PPIs on 32-bit ARM when both are
 * described by the device tree.  Keep the same policy here: U-Boot enters
 * in Secure state, but firmware/GIC security configuration can make the
 * CNTP event visible as either PPI 29 or PPI 30.  Both lines dispatch the
 * same timer handler, which clears the common compare event.
 */

/* The RV1106/RV1103 device tree uses SPIs through ID 126. */

#define NR_IRQS               160

#endif /* __VENDOR_ROCKCHIP_CHIPS_RV1103_INCLUDE_IRQ_H */
