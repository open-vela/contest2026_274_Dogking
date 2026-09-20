/****************************************************************************
 * vendor/allwinnertech/boards/a733/cubie-a7z/src/a7z_st7735.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/* Board glue for the common 1.8-inch 128x160 ST7735 module validated on
 * Cubie A7Z.  Pixel transfers and controller commands stay in the generic
 * NuttX ST7735 driver; this file owns only board wiring and reset.
 *
 *   SCK   PD11 / header pin 23 / SPI1_CLK
 *   MOSI  PD12 / header pin 19 / SPI1_MOSI
 *   CS    PD10 / header pin 24 / SPI1_CS0
 *   D/C   PL5  / header pin 22 / R_PIO GPIO
 *   RESET PB0  / header pin 7  / main PIO GPIO
 *   BL    3.3 V (always on)
 */

#include <nuttx/config.h>

#ifdef CONFIG_BOARD_A7Z_ST7735

#include <errno.h>
#include <stdint.h>

#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/lcd/lcd.h>
#include <nuttx/lcd/lcd_dev.h>
#include <nuttx/spi/spi.h>
#include <nuttx/lcd/st7735.h>

#include <arch/chip/a733_peripherals.h>

#include "arm64_internal.h"

#define A733_PIO_BASE          UINT64_C(0x02000000)
#define A733_PIO_STRIDE        UINT64_C(0x30)
#define A733_PB_BASE           (A733_PIO_BASE + A733_PIO_STRIDE)
#define A733_PB_CFG0           (A733_PB_BASE + 0x00)
#define A733_PB_DATA           (A733_PB_BASE + 0x10)
#define A733_ST7735_RESET_PIN  0

#define TAG "a7z_st7735"

static struct spi_dev_s *g_st7735_spi;
static struct lcd_dev_s *g_st7735_lcd;

static void a7z_st7735_reset_write(bool high)
{
  uint32_t value = getreg32(A733_PB_DATA);

  if (high)
    {
      value |= 1u << A733_ST7735_RESET_PIN;
    }
  else
    {
      value &= ~(1u << A733_ST7735_RESET_PIN);
    }

  putreg32(value, A733_PB_DATA);
}

int board_lcd_initialize(void)
{
  uint32_t value;

  syslog(LOG_INFO, "[%s] bring-up: SPI1 bus init\n", TAG);

  g_st7735_spi = a733_spibus_initialize(1);
  if (g_st7735_spi == NULL)
    {
      syslog(LOG_ERR, "[%s] SPI1 bus init failed\n", TAG);
      return -ENODEV;
    }

  /* Establish the inactive level before switching PB0 to output mode. */

  a7z_st7735_reset_write(true);
  value = getreg32(A733_PB_CFG0);
  value &= ~0x0fu;
  value |= 1u;
  putreg32(value, A733_PB_CFG0);

  /* Hardware reset timing exceeds the module's documented minima and
   * matches the sequence that was validated by the previous Linux port.
   */

  up_mdelay(5);
  a7z_st7735_reset_write(false);
  up_mdelay(20);
  a7z_st7735_reset_write(true);
  up_mdelay(150);

  syslog(LOG_INFO, "[%s] bring-up: RESET pulse done (PB0 high, SPI1 ready)\n",
         TAG);
  return OK;
}

struct lcd_dev_s *board_lcd_getdev(int devno)
{
  if (devno != 0 || g_st7735_spi == NULL)
    {
      return NULL;
    }

  if (g_st7735_lcd == NULL)
    {
      /* st7735_lcdinitialize() runs the panel command table and a full-screen
       * clear.  Over the polling SPI1 lower-half that clear is the single
       * longest step in the whole bring-up, so log both sides of it.
       */

      syslog(LOG_INFO, "[%s] bring-up: st7735_lcdinitialize start\n", TAG);
      g_st7735_lcd = st7735_lcdinitialize(g_st7735_spi);
      syslog(LOG_INFO, "[%s] bring-up: st7735_lcdinitialize done (%p)\n", TAG,
             (void *)g_st7735_lcd);
    }

  return g_st7735_lcd;
}

/****************************************************************************
 * Name: a7z_lcd_ensure_registered
 *
 * Description:
 *   Register /dev/lcd0 on first use.  board_lcd_getdev() performs the panel
 *   bring-up, so this is deliberately NOT called from board bring-up unless
 *   CONFIG_BOARD_A7Z_EARLY_ST7735 is selected.
 *
 * Returned Value:
 *   OK on success, or a negated errno on failure.
 *
 ****************************************************************************/

int a7z_lcd_ensure_registered(void)
{
  int ret;

  if (g_st7735_lcd != NULL)
    {
      return OK;
    }

  ret = board_lcd_initialize();
  if (ret < 0)
    {
      return ret;
    }

  ret = lcddev_register(0);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[%s] lcddev_register failed (%d)\n", TAG, ret);
      return ret;
    }

  syslog(LOG_INFO, "[%s] /dev/lcd0 registered on demand\n", TAG);
  return OK;
}

void board_lcd_uninitialize(void)
{
  if (g_st7735_lcd != NULL)
    {
      g_st7735_lcd->setpower(g_st7735_lcd, 0);
    }
}

#endif /* CONFIG_BOARD_A7Z_ST7735 */
