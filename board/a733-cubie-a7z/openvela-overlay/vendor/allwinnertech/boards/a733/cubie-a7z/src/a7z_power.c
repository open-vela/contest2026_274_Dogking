/****************************************************************************
 * vendor/allwinnertech/boards/a733/cubie-a7z/src/a7z_power.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>
#include <unistd.h>

#include <nuttx/arch.h>
#include <nuttx/board.h>

#ifdef CONFIG_BOARDCTL_POWEROFF
int board_power_off(int status)
{
  (void)status;
  sync();
  up_systempoweroff();
  return 0;
}
#endif

#ifdef CONFIG_BOARDCTL_RESET
int board_reset(int status)
{
  (void)status;
  sync();
  up_systemreset();
  return 0;
}
#endif
