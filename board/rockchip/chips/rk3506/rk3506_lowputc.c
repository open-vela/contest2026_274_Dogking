/****************************************************************************
 * vendor/rockchip/chips/rk3506/rk3506_lowputc.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <arch/chip/chip.h>

#include "arm_internal.h"

/* RK3506 UART0 is a Synopsys DesignWare APB UART with 32-bit-spaced,
 * 32-bit-access registers.
 */

#define UART_THR_OFFSET        0x0000
#define UART_LSR_OFFSET        0x0014
#define UART_LSR_THRE          (1 << 5)

void arm_lowputc(char ch)
{
  while ((getreg32(RK3506_UART0_BASE + UART_LSR_OFFSET) &
          UART_LSR_THRE) == 0)
    {
    }

  putreg32((uint32_t)(uint8_t)ch, RK3506_UART0_BASE + UART_THR_OFFSET);
}
