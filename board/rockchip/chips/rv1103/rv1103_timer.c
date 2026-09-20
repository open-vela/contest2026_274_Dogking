/****************************************************************************
 * vendor/rockchip/chips/rv1103/rv1103_timer.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/timers/arch_alarm.h>

#include "arm_internal.h"
#include "arm_timer.h"

void up_timer_initialize(void)
{
  up_alarm_set_lowerhalf(arm_timer_initialize(0));
}
