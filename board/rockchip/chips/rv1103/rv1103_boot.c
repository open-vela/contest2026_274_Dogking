/****************************************************************************
 * vendor/rockchip/chips/rv1103/rv1103_boot.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include "arm.h"
#include "arm_internal.h"
#include "rv1103_memorymap.h"

static void rv1103_bootmark(char step)
{
  arm_lowputc('R');
  arm_lowputc(step);
  arm_lowputc('\r');
  arm_lowputc('\n');
}

void arm_boot(void)
{
  rv1103_setupmappings();
  rv1103_bootmark('0');

  arm_fpuconfig();
  rv1103_bootmark('1');

#ifdef USE_EARLYSERIALINIT
  arm_earlyserialinit();
#endif

  rv1103_bootmark('2');
}
