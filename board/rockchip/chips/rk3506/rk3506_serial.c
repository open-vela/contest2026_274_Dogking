/****************************************************************************
 * vendor/rockchip/chips/rk3506/rk3506_serial.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/serial/uart_16550.h>

#include "arm_internal.h"

void arm_serialinit(void)
{
  u16550_serialinit();
}

#ifdef USE_EARLYSERIALINIT
void arm_earlyserialinit(void)
{
  u16550_earlyserialinit();
}
#endif
