/****************************************************************************
 * vendor/rockchip/boards/rv1103/luckfox-pico-mini/src/luckfox_gpio.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_DEV_GPIO

#include <stdbool.h>
#include <assert.h>

#include <nuttx/ioexpander/gpio.h>

#include <arch/chip/rv1103_gpio.h>
#include <arch/board/board.h>

struct luckfox_gpio_dev_s
{
  struct gpio_dev_s gpio;
};

static int luckfox_workled_read(struct gpio_dev_s *dev, bool *value)
{
  DEBUGASSERT(dev != NULL && value != NULL);

  *value = rv1103_gpio3_read(BOARD_WORK_LED_PIN);
  return 0;
}

static int luckfox_workled_write(struct gpio_dev_s *dev, bool value)
{
  DEBUGASSERT(dev != NULL);

  rv1103_gpio3_write(BOARD_WORK_LED_PIN, value);
  return 0;
}

static const struct gpio_operations_s g_workled_ops =
{
  .go_read   = luckfox_workled_read,
  .go_write  = luckfox_workled_write,
  .go_attach = NULL,
  .go_enable = NULL,
};

static struct luckfox_gpio_dev_s g_workled =
{
  .gpio =
    {
      .gp_pintype = GPIO_OUTPUT_PIN,
      .gp_ops     = &g_workled_ops,
    },
};

int luckfox_gpio_initialize(void)
{
  int ret;

  /* The official board DTS defines the work LED as active-high GPIO3_C6
   * with default-state = "on".  Preserve that behaviour.
   */

  ret = rv1103_gpio3_config_output(BOARD_WORK_LED_PIN, true);
  if (ret < 0)
    {
      return ret;
    }

  return gpio_pin_register_byname(&g_workled.gpio, "workled");
}

#endif /* CONFIG_DEV_GPIO */
