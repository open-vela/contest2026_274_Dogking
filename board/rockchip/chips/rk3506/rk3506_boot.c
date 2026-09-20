/****************************************************************************
 * vendor/rockchip/chips/rk3506/rk3506_boot.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include "arm.h"
#include "arm_internal.h"
#include "rk3506_memorymap.h"

/****************************************************************************
 * Name: arm_boot
 *
 * Description:
 *   Complete the C-level portion of arm_head.S startup.  Rockchip U-Boot has
 *   already initialized DDR, clocks and UART0 before entering this image.
 ****************************************************************************/

void arm_boot(void)
{
  /* arm_head.S established the cacheable DDR mapping.  Add the device
   * mapping before any driver accesses RK3506 registers.
   */

  rk3506_setupmappings();

  /* Cortex-A7 has VFPv4/NEON. */

  arm_fpuconfig();

#ifdef USE_EARLYSERIALINIT
  arm_earlyserialinit();
#endif
}
