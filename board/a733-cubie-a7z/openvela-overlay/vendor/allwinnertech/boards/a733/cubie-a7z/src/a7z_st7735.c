/****************************************************************************
 * vendor/allwinnertech/boards/a733/cubie-a7z/src/a7z_st7735.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/* Board support for the common 1.8-inch 128x160 ST7735 module validated on
 * Cubie A7Z.  It owns board wiring, reset, the panel-specific power/gamma
 * sequence, and an explicit 8-bit RGB565 transfer path.  The direct path is
 * deliberate: it avoids native-endian ambiguity in generic 16-bit SPI words.
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
#include <stdbool.h>
#include <stdint.h>

#include <nuttx/arch.h>
#include <nuttx/lcd/lcd.h>
#include <nuttx/spi/spi.h>
#include <nuttx/lcd/st7735.h>

#include <arch/chip/a733_peripherals.h>

#include "arm64_internal.h"

#define A733_PIO_BASE          UINT64_C(0x02000000)
#define A733_PIO_FIRST_BANK    UINT64_C(0x80)
#define A733_PIO_STRIDE        UINT64_C(0x80)
#define A733_PB_BASE           (A733_PIO_BASE + A733_PIO_FIRST_BANK + \
                                A733_PIO_STRIDE)
#define A733_PB_CFG0           (A733_PB_BASE + 0x00)
#define A733_PB_DATA           (A733_PB_BASE + 0x10)
#define A733_ST7735_RESET_PIN  0
#define A733_ST7735_DEVID      SPIDEV_DISPLAY(0)
#define A733_ST7735_FREQUENCY  12000000

#define ST7735_SWRESET         0x01
#define ST7735_SLPOUT          0x11
#define ST7735_NORON           0x13
#define ST7735_INVOFF          0x20
#define ST7735_DISPON          0x29
#define ST7735_CASET           0x2a
#define ST7735_RASET           0x2b
#define ST7735_RAMWR           0x2c
#define ST7735_MADCTL          0x36
#define ST7735_COLMOD          0x3a
#define ST7735_FRMCTR1         0xb1
#define ST7735_FRMCTR2         0xb2
#define ST7735_FRMCTR3         0xb3
#define ST7735_INVCTR          0xb4
#define ST7735_PWCTR1          0xc0
#define ST7735_PWCTR2          0xc1
#define ST7735_PWCTR3          0xc2
#define ST7735_PWCTR4          0xc3
#define ST7735_PWCTR5          0xc4
#define ST7735_VMCTR1          0xc5
#define ST7735_GMCTRP1         0xe0
#define ST7735_GMCTRN1         0xe1

static struct spi_dev_s *g_st7735_spi;
static struct lcd_dev_s *g_st7735_lcd;
static bool g_st7735_panel_ready;

/* The Linux implementation validated with this exact module uses portrait
 * rotation (MX|MY) and BGR order selected by a clear RGB bit.  Keep the
 * byte-for-byte value here instead of depending on generic-driver defaults.
 */

#define A733_ST7735_MADCTL     0xc0

static int a7z_st7735_command(uint8_t command, const uint8_t *data,
                              size_t length, unsigned int delay_ms)
{
  int ret;

  SPI_SELECT(g_st7735_spi, A733_ST7735_DEVID, true);
  ret = SPI_CMDDATA(g_st7735_spi, A733_ST7735_DEVID, true);
  if (ret >= 0)
    {
      SPI_SEND(g_st7735_spi, command);
      ret = a733_spibus_last_error(1);
      if (length > 0)
        {
          if (ret >= 0)
            {
              ret = SPI_CMDDATA(g_st7735_spi, A733_ST7735_DEVID, false);
            }

          if (ret >= 0)
            {
              SPI_SNDBLOCK(g_st7735_spi, data, length);
              ret = a733_spibus_last_error(1);
            }
        }
    }

  SPI_SELECT(g_st7735_spi, A733_ST7735_DEVID, false);
  if (delay_ms > 0)
    {
      up_mdelay(delay_ms);
    }

  return ret;
}

static int a7z_st7735_panel_initialize(void)
{
  static const uint8_t frmctr[] = {0x01, 0x2c, 0x2d};
  static const uint8_t frmctr3[] = {0x01, 0x2c, 0x2d,
                                    0x01, 0x2c, 0x2d};
  static const uint8_t invctr[] = {0x07};
  static const uint8_t pwctr1[] = {0xa2, 0x02, 0x84};
  static const uint8_t pwctr2[] = {0xc5};
  static const uint8_t pwctr3[] = {0x0a, 0x00};
  static const uint8_t pwctr4[] = {0x8a, 0x2a};
  static const uint8_t pwctr5[] = {0x8a, 0xee};
  static const uint8_t vmctr1[] = {0x0e};
  static const uint8_t colmod[] = {0x05};
  static const uint8_t madctl[] = {A733_ST7735_MADCTL};
  static const uint8_t gamma_positive[] =
    {
      0x02, 0x1c, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2d,
      0x29, 0x25, 0x2b, 0x39, 0x00, 0x01, 0x03, 0x10
    };
  static const uint8_t gamma_negative[] =
    {
      0x03, 0x1d, 0x07, 0x06, 0x2e, 0x2c, 0x29, 0x2d,
      0x2e, 0x2e, 0x37, 0x3f, 0x00, 0x00, 0x02, 0x10
    };
  int ret;

#define PANEL_COMMAND(cmd, data, delay) \
  do \
    { \
      ret = a7z_st7735_command((cmd), (data), sizeof(data), (delay)); \
      if (ret < 0) \
        { \
          goto out; \
        } \
    } \
  while (0)

#define PANEL_COMMAND_ONLY(cmd, delay) \
  do \
    { \
      ret = a7z_st7735_command((cmd), NULL, 0, (delay)); \
      if (ret < 0) \
        { \
          goto out; \
        } \
    } \
  while (0)

  ret = SPI_LOCK(g_st7735_spi, true);
  if (ret < 0)
    {
      return ret;
    }

  SPI_SETMODE(g_st7735_spi, SPIDEV_MODE0);
  SPI_SETBITS(g_st7735_spi, 8);
  SPI_SETFREQUENCY(g_st7735_spi, A733_ST7735_FREQUENCY);

  /* This is the sequence validated by the earlier Linux driver for this
   * exact 1.8-inch module.  Window addressing and RGB565 transfers used by
   * the A733 display app are handled by a7z_st7735_write_area() below.
   */

  PANEL_COMMAND_ONLY(ST7735_SWRESET, 150);
  PANEL_COMMAND_ONLY(ST7735_SLPOUT, 500);
  PANEL_COMMAND(ST7735_FRMCTR1, frmctr, 0);
  PANEL_COMMAND(ST7735_FRMCTR2, frmctr, 0);
  PANEL_COMMAND(ST7735_FRMCTR3, frmctr3, 0);
  PANEL_COMMAND(ST7735_INVCTR, invctr, 0);
  PANEL_COMMAND(ST7735_PWCTR1, pwctr1, 0);
  PANEL_COMMAND(ST7735_PWCTR2, pwctr2, 0);
  PANEL_COMMAND(ST7735_PWCTR3, pwctr3, 0);
  PANEL_COMMAND(ST7735_PWCTR4, pwctr4, 0);
  PANEL_COMMAND(ST7735_PWCTR5, pwctr5, 0);
  PANEL_COMMAND(ST7735_VMCTR1, vmctr1, 0);
  PANEL_COMMAND_ONLY(ST7735_INVOFF, 0);
  PANEL_COMMAND(ST7735_COLMOD, colmod, 0);
  PANEL_COMMAND(ST7735_MADCTL, madctl, 0);
  PANEL_COMMAND(ST7735_GMCTRP1, gamma_positive, 0);
  PANEL_COMMAND(ST7735_GMCTRN1, gamma_negative, 0);
  PANEL_COMMAND_ONLY(ST7735_NORON, 10);
  PANEL_COMMAND_ONLY(ST7735_DISPON, 100);
  g_st7735_panel_ready = true;

out:
  SPI_LOCK(g_st7735_spi, false);
#undef PANEL_COMMAND
#undef PANEL_COMMAND_ONLY
  return ret;
}

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
  int ret;
  uint32_t value;

  if (g_st7735_panel_ready)
    {
      return OK;
    }

  g_st7735_spi = a733_spibus_initialize(1);
  if (g_st7735_spi == NULL)
    {
      return -ENODEV;
    }

  /* A timeout is latched for the whole panel transaction so a missing or
   * broken bus cannot multiply into hundreds of per-row waits.  A new,
   * explicit initialization attempt starts with clean FIFOs/status.
   */

  a733_spibus_clear_error(1);

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

  ret = a7z_st7735_panel_initialize();
  if (ret < 0)
    {
      return ret;
    }

  return OK;
}

bool a7z_st7735_is_ready(void)
{
  return g_st7735_panel_ready;
}

int a7z_st7735_write_area(uint16_t x0, uint16_t y0,
                          uint16_t x1, uint16_t y1,
                          const uint16_t *pixels, size_t npixels)
{
  uint8_t address[4];
  uint8_t bytes[128 * 2];
  size_t offset = 0;
  size_t expected;
  int ret;

  if (pixels == NULL || x0 > x1 || y0 > y1 || x1 >= 128 || y1 >= 160)
    {
      return -EINVAL;
    }

  expected = (size_t)(x1 - x0 + 1) * (size_t)(y1 - y0 + 1);
  if (npixels != expected)
    {
      return -EINVAL;
    }

  ret = board_lcd_initialize();
  if (ret < 0)
    {
      return ret;
    }

  ret = SPI_LOCK(g_st7735_spi, true);
  if (ret < 0)
    {
      return ret;
    }

  SPI_SETMODE(g_st7735_spi, SPIDEV_MODE0);
  SPI_SETBITS(g_st7735_spi, 8);
  SPI_SETFREQUENCY(g_st7735_spi, A733_ST7735_FREQUENCY);
  a733_spibus_clear_error(1);

  address[0] = x0 >> 8;
  address[1] = x0 & 0xff;
  address[2] = x1 >> 8;
  address[3] = x1 & 0xff;
  ret = a7z_st7735_command(ST7735_CASET, address, sizeof(address), 0);

  if (ret >= 0)
    {
      address[0] = y0 >> 8;
      address[1] = y0 & 0xff;
      address[2] = y1 >> 8;
      address[3] = y1 & 0xff;
      ret = a7z_st7735_command(ST7735_RASET, address, sizeof(address), 0);
    }

  if (ret >= 0)
    {
      ret = a7z_st7735_command(ST7735_RAMWR, NULL, 0, 0);
    }

  /* Never use the generic 16-bit SPI-word path here.  Serialize RGB565
   * explicitly as the ST7735 wire format: high byte, then low byte.  This
   * removes native-endian and FIFO-word-width ambiguity responsible for the
   * regular vertical stripes seen on the earlier hardware image.
   */

  if (ret >= 0)
    {
      SPI_SELECT(g_st7735_spi, A733_ST7735_DEVID, true);
      ret = SPI_CMDDATA(g_st7735_spi, A733_ST7735_DEVID, false);
      while (ret >= 0 && offset < npixels)
        {
          size_t count = npixels - offset;
          size_t i;

          if (count > 128)
            {
              count = 128;
            }

          for (i = 0; i < count; i++)
            {
              uint16_t pixel = pixels[offset + i];
              bytes[2 * i] = pixel >> 8;
              bytes[2 * i + 1] = pixel & 0xff;
            }

          SPI_SNDBLOCK(g_st7735_spi, bytes, count * 2);
          ret = a733_spibus_last_error(1);
          offset += count;
        }

      SPI_SELECT(g_st7735_spi, A733_ST7735_DEVID, false);
    }

  SPI_LOCK(g_st7735_spi, false);
  return ret;
}

struct lcd_dev_s *board_lcd_getdev(int devno)
{
  if (devno != 0 || g_st7735_spi == NULL)
    {
      return NULL;
    }

  if (g_st7735_lcd == NULL)
    {
      g_st7735_lcd = st7735_lcdinitialize(g_st7735_spi);
    }

  return g_st7735_lcd;
}

void board_lcd_uninitialize(void)
{
  if (g_st7735_lcd != NULL)
    {
      g_st7735_lcd->setpower(g_st7735_lcd, 0);
    }

  g_st7735_panel_ready = false;
}

#endif /* CONFIG_BOARD_A7Z_ST7735 */
