/****************************************************************************
 * vendor/rockchip/chips/rv1103/rv1103_serial.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/serial/uart_16550.h>
#include <arch/chip/rv1103_periph.h>

#include "arm_internal.h"

void arm_serialinit(void)
{
  rv1103_extended_uart_setup();
  u16550_serialinit();
}

#ifdef USE_EARLYSERIALINIT
void arm_earlyserialinit(void)
{
  rv1103_extended_uart_setup();
  u16550_earlyserialinit();
}
#endif
