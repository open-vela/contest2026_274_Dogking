/****************************************************************************
 * vendor/rockchip/chips/rk3506/rk3506_gpio.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <errno.h>

#include <arch/chip/rk3506_gpio.h>

#include "arm_internal.h"

/* Register definitions are derived from Rockchip's RK3506 CMSIS header and
 * hal_gpio/hal_pinctrl implementations in the Luckfox SDK.  RK3506 GPIO uses
 * write-mask registers: bits 31:16 select fields and bits 15:0 carry values.
 */

#define RK3506_CRU_BASE               0xff9a0000u
#define RK3506_CRU_GATE_CON03         (RK3506_CRU_BASE + 0x080cu)
#define RK3506_CRU_GATE_CON22         (RK3506_CRU_BASE + 0x0858u)
#define RK3506_PCLK_GPIO1_SHIFT       8
#define RK3506_PCLK_GPIO1_IOC_SHIFT   1

#define RK3506_GPIO1_BASE             0xff870000u
#define RK3506_GPIO_SWPORT_DR_L       0x0000u
#define RK3506_GPIO_SWPORT_DDR_L      0x0008u
#define RK3506_GPIO_EXT_PORT          0x0070u

#define RK3506_GPIO1_IOC_BASE         0xff660000u
#define RK3506_GPIO1A_IOMUX_SEL_0     0x0020u

#define RK3506_GPIO1_NPINS            28
#define RK3506_GPIO_PINS_PER_GROUP    8
#define RK3506_GPIO_PINS_PER_IOMUX    4

static inline void rk3506_gpio1_enable_clocks(void)
{
  /* A zero gate bit enables a Rockchip clock. */

  putreg32(1u << (RK3506_PCLK_GPIO1_SHIFT + 16),
           RK3506_CRU_GATE_CON03);
  putreg32(1u << (RK3506_PCLK_GPIO1_IOC_SHIFT + 16),
           RK3506_CRU_GATE_CON22);
}

static inline uintptr_t rk3506_gpio1_halfreg(unsigned int pin,
                                             unsigned int offset)
{
  return RK3506_GPIO1_BASE + offset + (pin >= 16 ? 4u : 0u);
}

static inline uint32_t rk3506_gpio1_halfbit(unsigned int pin)
{
  return 1u << (pin & 15u);
}

static void rk3506_gpio1_set_iomux_gpio(unsigned int pin)
{
  unsigned int shift = (pin % RK3506_GPIO_PINS_PER_IOMUX) * 4;
  uintptr_t reg;

  reg = RK3506_GPIO1_IOC_BASE + RK3506_GPIO1A_IOMUX_SEL_0;
  reg += (pin / RK3506_GPIO_PINS_PER_GROUP) * 8;
  reg += ((pin % RK3506_GPIO_PINS_PER_GROUP) /
          RK3506_GPIO_PINS_PER_IOMUX) * 4;

  /* Function zero selects GPIO. */

  putreg32(0x0fu << (shift + 16), reg);
}

void rk3506_gpio1_write(unsigned int pin, bool value)
{
  uint32_t bit;
  uint32_t regval;

  if (pin >= RK3506_GPIO1_NPINS)
    {
      return;
    }

  bit = rk3506_gpio1_halfbit(pin);
  regval = bit << 16;
  if (value)
    {
      regval |= bit;
    }

  putreg32(regval,
           rk3506_gpio1_halfreg(pin, RK3506_GPIO_SWPORT_DR_L));
}

bool rk3506_gpio1_read(unsigned int pin)
{
  if (pin >= RK3506_GPIO1_NPINS)
    {
      return false;
    }

  return (getreg32(RK3506_GPIO1_BASE + RK3506_GPIO_EXT_PORT) &
          (1u << pin)) != 0;
}

int rk3506_gpio1_config_output(unsigned int pin, bool initial)
{
  uint32_t bit;

  if (pin >= RK3506_GPIO1_NPINS)
    {
      return -EINVAL;
    }

  rk3506_gpio1_enable_clocks();
  rk3506_gpio1_set_iomux_gpio(pin);

  /* Set the inactive value before enabling the output driver so the board
   * does not see a transient pulse.
   */

  rk3506_gpio1_write(pin, initial);

  bit = rk3506_gpio1_halfbit(pin);
  putreg32((bit << 16) | bit,
           rk3506_gpio1_halfreg(pin, RK3506_GPIO_SWPORT_DDR_L));
  return 0;
}
