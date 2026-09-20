/****************************************************************************
 * vendor/rockchip/boards/rk3506/luckfox-lyra-zero-w/src/luckfox_gpio.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_DEV_GPIO

#include <stdbool.h>
#include <assert.h>

#include <nuttx/ioexpander/gpio.h>

#include <arch/chip/rk3506_gpio.h>

#include <arch/board/board.h>

struct luckfox_gpio_dev_s
{
  struct gpio_dev_s gpio;
};

static int luckfox_actled_read(struct gpio_dev_s *dev, bool *value)
{
  DEBUGASSERT(dev != NULL && value != NULL);

  *value = rk3506_gpio1_read(BOARD_ACT_LED_PIN);
  return 0;
}

static int luckfox_actled_write(struct gpio_dev_s *dev, bool value)
{
  DEBUGASSERT(dev != NULL);

  rk3506_gpio1_write(BOARD_ACT_LED_PIN, value);
  return 0;
}

static const struct gpio_operations_s g_actled_ops =
{
  .go_read   = luckfox_actled_read,
  .go_write  = luckfox_actled_write,
  .go_attach = NULL,
  .go_enable = NULL,
};

static struct luckfox_gpio_dev_s g_actled =
{
  .gpio =
    {
      .gp_pintype = GPIO_OUTPUT_PIN,
      .gp_ops     = &g_actled_ops,
    },
};

int luckfox_gpio_initialize(void)
{
  int ret;

  /* The schematic shows GPIO1_A0 driving an NPN transistor.  High turns the
   * activity LED on; initialize it off.
   */

  ret = rk3506_gpio1_config_output(BOARD_ACT_LED_PIN, false);
  if (ret < 0)
    {
      return ret;
    }

  return gpio_pin_register_byname(&g_actled.gpio, "actled");
}

#endif /* CONFIG_DEV_GPIO */
