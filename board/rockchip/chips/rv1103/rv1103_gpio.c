/****************************************************************************
 * vendor/rockchip/chips/rv1103/rv1103_gpio.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <errno.h>

#include <arch/chip/rv1103_gpio.h>

#include "arm_internal.h"

/* Register locations and clock gates are taken from the RV1106/RV1103
 * Linux BSP.  This SoC uses Rockchip write-mask registers: the upper half
 * selects which lower-half bits are updated.
 */

#define RV1103_CRU_BASE                0xff3a0000u
#define RV1103_VI_CRU_BASE             0x00014000u
#define RV1103_VI_CLKGATE_BASE         0x00000800u
#define RV1103_VI_CLKGATE_CON(n)       \
  (RV1103_CRU_BASE + RV1103_VI_CRU_BASE + \
   RV1103_VI_CLKGATE_BASE + ((n) * 4u))
#define RV1103_PCLK_GPIO3_SHIFT        15

#define RV1103_GPIO3_BASE              0xff550000u
#define RV1103_GPIO_SWPORT_DR_L        0x0000u
#define RV1103_GPIO_SWPORT_DDR_L       0x0008u
#define RV1103_GPIO_EXT_PORT           0x0070u

#define RV1103_IOC_BASE                0xff538000u
#define RV1103_GPIO3C_IOMUX_SEL_0      0x00020050u

#define RV1103_GPIO3_NPINS             32
#define RV1103_GPIO_PINS_PER_GROUP     8
#define RV1103_GPIO_PINS_PER_IOMUX     4

static inline void rv1103_gpio3_enable_clock(void)
{
  /* PCLK_GPIO3 is VI_CLKGATE_CON(1)[15].  A zero gate bit enables a
   * Rockchip clock, so only the corresponding upper write-mask bit is set.
   */

  putreg32(1u << (RV1103_PCLK_GPIO3_SHIFT + 16),
           RV1103_VI_CLKGATE_CON(1));
}

static inline uintptr_t rv1103_gpio3_halfreg(unsigned int pin,
                                              unsigned int offset)
{
  return RV1103_GPIO3_BASE + offset + (pin >= 16 ? 4u : 0u);
}

static inline uint32_t rv1103_gpio3_halfbit(unsigned int pin)
{
  return 1u << (pin & 15u);
}

static void rv1103_gpio3_set_iomux_gpio(unsigned int pin)
{
  unsigned int shift = (pin % RV1103_GPIO_PINS_PER_IOMUX) * 4;
  uintptr_t reg;

  /* The GPIO3A/B/C/D mux blocks are eight bytes apart.  Each block has
   * one register for pins 0..3 and another for pins 4..7.
   */

  reg = RV1103_IOC_BASE + RV1103_GPIO3C_IOMUX_SEL_0;
  reg += ((pin / RV1103_GPIO_PINS_PER_GROUP) - 2u) * 8u;
  reg += ((pin % RV1103_GPIO_PINS_PER_GROUP) /
          RV1103_GPIO_PINS_PER_IOMUX) * 4u;

  /* Function zero selects GPIO. */

  putreg32(0x0fu << (shift + 16), reg);
}

void rv1103_gpio3_write(unsigned int pin, bool value)
{
  uint32_t bit;
  uint32_t regval;

  if (pin >= RV1103_GPIO3_NPINS)
    {
      return;
    }

  bit = rv1103_gpio3_halfbit(pin);
  regval = bit << 16;
  if (value)
    {
      regval |= bit;
    }

  putreg32(regval,
           rv1103_gpio3_halfreg(pin, RV1103_GPIO_SWPORT_DR_L));
}

bool rv1103_gpio3_read(unsigned int pin)
{
  if (pin >= RV1103_GPIO3_NPINS)
    {
      return false;
    }

  return (getreg32(RV1103_GPIO3_BASE + RV1103_GPIO_EXT_PORT) &
          (1u << pin)) != 0;
}

int rv1103_gpio3_config_output(unsigned int pin, bool initial)
{
  uint32_t bit;

  /* This first implementation intentionally supports GPIO3_C/D only. */

  if (pin < 16 || pin >= RV1103_GPIO3_NPINS)
    {
      return -EINVAL;
    }

  rv1103_gpio3_enable_clock();
  rv1103_gpio3_set_iomux_gpio(pin);

  /* Program the level before enabling the output driver to avoid a pulse. */

  rv1103_gpio3_write(pin, initial);

  bit = rv1103_gpio3_halfbit(pin);
  putreg32((bit << 16) | bit,
           rv1103_gpio3_halfreg(pin, RV1103_GPIO_SWPORT_DDR_L));
  return 0;
}
