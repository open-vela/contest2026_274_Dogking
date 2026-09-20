/****************************************************************************
 * vendor/rockchip/chips/rv1103/rv1103_irq.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/arch.h>

#include <assert.h>

#include "arm_internal.h"
#include "gic.h"
#include "sctlr.h"

extern uint8_t _vector_start[];

void up_irqinitialize(void)
{
#ifdef CONFIG_ARCH_LOWVECTORS
  DEBUGASSERT((((uintptr_t)_vector_start) & ~VBAR_MASK) == 0);
  cp15_wrvbar((uint32_t)_vector_start);
#endif

  arm_gic0_initialize();
  arm_gic_initialize();

#ifndef CONFIG_SUPPRESS_INTERRUPTS
  arm_color_intstack();
  up_irq_enable();
#endif
}
