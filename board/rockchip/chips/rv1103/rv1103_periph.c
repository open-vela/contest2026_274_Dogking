/****************************************************************************
 * vendor/rockchip/chips/rv1103/rv1103_periph.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <nuttx/arch.h>
#include <arch/chip/rv1103_periph.h>

#include "arm_internal.h"

#define GPIO0_IOC  0xff388000u
#define GPIO1_IOC  0xff538000u
#define GPIO2_IOC  0xff548000u
#define GPIO3_IOC  0xff558000u
#define GPIO4_IOC  0xff568000u
#define CRU         0xff3a0000u
#define PERICRU     0xff3b2000u
#define VEPUCRU     0xff3ba000u
#define WMASK(m,v)  (((uint32_t)(m) << 16) | ((uint32_t)(v) & (m)))

struct rv1103_ioc_desc_s
{
  uintptr_t base;
  uint16_t mux[4];
  uint16_t pull[4];
  uint16_t smt[4];
};

/* Register offsets are taken from the official RV1106 HAL register
 * structures.  A value of 0xffff marks a group which is not bonded out.
 */

static const struct rv1103_ioc_desc_s g_ioc[] =
{
  {GPIO0_IOC, {0x0000, 0xffff, 0xffff, 0xffff},
              {0x0038, 0xffff, 0xffff, 0xffff},
              {0x0058, 0xffff, 0xffff, 0xffff}},
  {GPIO1_IOC, {0x0000, 0x0008, 0x0010, 0x0018},
              {0x01c0, 0x01c4, 0x01c8, 0x01cc},
              {0x0280, 0x0284, 0x0288, 0x028c}},
  {GPIO2_IOC, {0x0020, 0x0028, 0xffff, 0xffff},
              {0x01d0, 0x01d4, 0xffff, 0xffff},
              {0x0290, 0x0294, 0xffff, 0xffff}},
  {GPIO3_IOC, {0x0040, 0x0048, 0x0050, 0x0058},
              {0x01e0, 0xffff, 0x01e8, 0x01ec},
              {0x02a0, 0xffff, 0x02a8, 0x02ac}},
  {GPIO4_IOC, {0x0000, 0x0008, 0x0010, 0xffff},
              {0x0070, 0x0074, 0xffff, 0xffff},
              {0x00a0, 0x00a4, 0xffff, 0xffff}}
};

int rv1103_pinctrl_config(unsigned int bank, unsigned int pin,
                          unsigned int function, enum rv1103_pull_e pull,
                          bool schmitt)
{
  FAR const struct rv1103_ioc_desc_s *d;
  unsigned int group;
  unsigned int index;
  unsigned int shift;

  if (bank >= sizeof(g_ioc) / sizeof(g_ioc[0]) || pin >= 32 ||
      function > 15)
    {
      return -EINVAL;
    }

  d = &g_ioc[bank];
  group = pin >> 3;
  index = pin & 7;
  if (d->mux[group] == 0xffff || d->pull[group] == 0xffff ||
      d->smt[group] == 0xffff)
    {
      return -ENODEV;
    }

  /* Four mux bits per pin, four pins per write-mask register. */

  shift = (index & 3) * 4;
  putreg32(WMASK(0xfu << shift, function << shift),
           d->base + d->mux[group] + (index >= 4 ? 4 : 0));

  /* Two pull bits and one input/Schmitt bit per pin. */

  shift = index * 2;
  putreg32(WMASK(3u << shift, (uint32_t)pull << shift),
           d->base + d->pull[group]);
  shift = index;
  putreg32(WMASK(1u << shift, (schmitt ? 1u : 0u) << shift),
           d->base + d->smt[group]);
  return 0;
}

static void rv1103_ungate(uintptr_t reg, uint32_t mask)
{
  /* Rockchip CRU gate bits are active high. */

  putreg32(WMASK(mask, 0), reg);
}

void rv1103_periph_clock_i2c(int bus)
{
  if (bus == 3)
    {
      putreg32(WMASK(3u << 14, 3u << 14), PERICRU + 0x0308);
      rv1103_ungate(PERICRU + 0x0804, (1u << 12) | (1u << 13));
    }
  else if (bus == 4)
    {
      putreg32(WMASK(3u, 3u), PERICRU + 0x030c);
      rv1103_ungate(PERICRU + 0x0804, (1u << 14) | (1u << 15));
    }
}

void rv1103_periph_clock_spi(int bus)
{
  if (bus == 0)
    {
      putreg32(WMASK(3u << 12, 3u << 12), VEPUCRU + 0x0300);
      rv1103_ungate(VEPUCRU + 0x0804, (1u << 2) | (1u << 3));
    }
  else if (bus == 1)
    {
      /* SPI1 lives in PERICRU.  U-Boot already supplies its parent; only
       * ungate the APB and functional clocks here.
       */

      rv1103_ungate(PERICRU + 0x080c, (1u << 6) | (1u << 7));
    }
}

void rv1103_periph_clock_pwm(int block)
{
  if (block == 0)
    {
      putreg32(WMASK(3u, 2u), PERICRU + 0x032c);
      rv1103_ungate(PERICRU + 0x081c, (1u << 3) | (1u << 4));
    }
  else if (block == 1)
    {
      putreg32(WMASK(3u << 9, 2u << 9), PERICRU + 0x031c);
      rv1103_ungate(PERICRU + 0x080c, 1u << 15);
      rv1103_ungate(PERICRU + 0x0810, 1u << 0);
    }
  else if (block == 2)
    {
      putreg32(WMASK(3u << 11, 2u << 11), PERICRU + 0x031c);
      rv1103_ungate(PERICRU + 0x0810, (1u << 2) | (1u << 3));
    }
}

void rv1103_periph_clock_saradc(void)
{
  /* 24 MHz / 8 = 3 MHz, within the official SARADC clock limit. */

  putreg32(WMASK(7u, 7u), PERICRU + 0x031c);
  rv1103_ungate(PERICRU + 0x080c, (1u << 3) | (1u << 4));
}

void rv1103_periph_clock_uart(int uart)
{
  if (uart == 3)
    {
      rv1103_ungate(CRU + 0x0804, (1u << 4) | (1u << 6));
      rv1103_ungate(PERICRU + 0x0808, (1u << 7) | (1u << 10));
      putreg32(WMASK(3u, 2u), CRU + 0x0334); /* direct 24 MHz */
    }
  else if (uart == 4)
    {
      rv1103_ungate(CRU + 0x0804, (1u << 7) | (1u << 9));
      rv1103_ungate(PERICRU + 0x0808, (1u << 11) | (1u << 14));
      putreg32(WMASK(3u, 2u), CRU + 0x033c); /* direct 24 MHz */
    }
}

void rv1103_extended_uart_setup(void)
{
#ifdef CONFIG_RV1103_UART3_M1
  rv1103_periph_clock_uart(3);
  rv1103_pinctrl_config(1, 25, 5, RV1103_PULL_UP, true); /* GPIO1_D1 RX */
  rv1103_pinctrl_config(1, 24, 5, RV1103_PULL_UP, true); /* GPIO1_D0 TX */
#endif
#ifdef CONFIG_RV1103_UART4_M1
  rv1103_periph_clock_uart(4);
  rv1103_pinctrl_config(1, 20, 4, RV1103_PULL_UP, true); /* GPIO1_C4 RX */
  rv1103_pinctrl_config(1, 21, 4, RV1103_PULL_UP, true); /* GPIO1_C5 TX */
#endif
}
