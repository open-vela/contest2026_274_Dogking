/****************************************************************************
 * vendor/allwinnertech/chips/a733/include/irq.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __VENDOR_ALLWINNERTECH_CHIPS_A733_INCLUDE_IRQ_H
#define __VENDOR_ALLWINNERTECH_CHIPS_A733_INCLUDE_IRQ_H

/* ARM architectural timer PPIs.  The A733 device tree routes the virtual
 * timer on PPI 11, which is global interrupt ID 27.
 */

#define A733_IRQ_VTIMER          27

/* GIC SPI numbers in the device tree are relative to global interrupt 32. */

#define A733_IRQ_UART0           (32 + 2)
#define A733_IRQ_SDMMC0          (32 + 74)

/* sun60iw2p1 exposes interrupt specifiers above SPI 400.  Keep the complete
 * GIC interrupt namespace available so later peripheral drivers do not need
 * to change the architectural port.
 */

#define NR_IRQS                  512

#endif /* __VENDOR_ALLWINNERTECH_CHIPS_A733_INCLUDE_IRQ_H */
