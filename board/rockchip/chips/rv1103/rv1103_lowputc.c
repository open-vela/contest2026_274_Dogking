/****************************************************************************
 * vendor/rockchip/chips/rv1103/rv1103_lowputc.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <arch/chip/chip.h>

#include "arm_internal.h"

#define UART_THR_OFFSET        0x0000
#define UART_LSR_OFFSET        0x0014
#define UART_LSR_THRE          (1 << 5)

void arm_lowputc(char ch)
{
  while ((getreg32(RV1103_UART2_BASE + UART_LSR_OFFSET) &
          UART_LSR_THRE) == 0)
    {
    }

  putreg32((uint32_t)(uint8_t)ch, RV1103_UART2_BASE + UART_THR_OFFSET);
}
