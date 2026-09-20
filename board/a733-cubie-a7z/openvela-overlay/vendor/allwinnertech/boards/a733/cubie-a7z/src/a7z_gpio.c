/****************************************************************************
 * vendor/allwinnertech/boards/a733/cubie-a7z/src/a7z_gpio.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BOARD_A7Z_USER_LED

#include <stdbool.h>
#include <stdint.h>

#include <nuttx/ioexpander/gpio.h>

#include "arm64_internal.h"

/* The official Cubie A7Z DTS assigns the active-high blue user LED to PM2.
 * A733 R_PIO starts at 0x07025000.  Bank L is slot zero and bank M slot one;
 * Allwinner PIO banks use a 0x30 byte stride.
 */

#define A733_R_PIO_BASE       UINT64_C(0x07025000)
#define A733_PM_BASE          (A733_R_PIO_BASE + 0x30)
#define A733_PM_CFG0          (A733_PM_BASE + 0x00)
#define A733_PM_DATA          (A733_PM_BASE + 0x10)
#define A733_PM2_SHIFT        8
#define A733_GPIO_OUTPUT      1

struct a7z_led_s
{
  struct gpio_dev_s gpio;
};

static int a7z_led_read(struct gpio_dev_s *dev, bool *value)
{
  (void)dev;
  *value = (getreg32(A733_PM_DATA) & (1u << 2)) != 0;
  return 0;
}

static int a7z_led_write(struct gpio_dev_s *dev, bool value)
{
  uint32_t reg;

  (void)dev;
  reg = getreg32(A733_PM_DATA);
  if (value)
    {
      reg |= 1u << 2;
    }
  else
    {
      reg &= ~(1u << 2);
    }

  putreg32(reg, A733_PM_DATA);
  return 0;
}

static const struct gpio_operations_s g_a7z_led_ops =
{
  .go_read = a7z_led_read,
  .go_write = a7z_led_write,
  .go_attach = NULL,
  .go_enable = NULL,
};

static struct a7z_led_s g_a7z_led =
{
  .gpio =
    {
      .gp_pintype = GPIO_OUTPUT_PIN,
      .gp_ops = &g_a7z_led_ops,
    },
};

int a7z_gpio_initialize(void)
{
  uint32_t reg;

  reg = getreg32(A733_PM_CFG0);
  reg &= ~(0x0fu << A733_PM2_SHIFT);
  reg |= A733_GPIO_OUTPUT << A733_PM2_SHIFT;
  putreg32(reg, A733_PM_CFG0);

  return gpio_pin_register_byname(&g_a7z_led.gpio, "userled");
}

#endif
