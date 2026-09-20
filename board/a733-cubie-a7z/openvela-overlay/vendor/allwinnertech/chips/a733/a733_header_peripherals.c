/****************************************************************************
 * vendor/allwinnertech/chips/a733/a733_header_peripherals.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/* Conservative polling lower halves for the Cubie A7Z expansion buses.
 * Pin assignments and mux values are taken from the official sun60iw2
 * pinctrl table and the Cubie A7Z v1.11 schematic:
 *
 *   TWI2  PD16/PD17, function 6  -> /dev/i2c2
 *   TWI7  PJ22/PJ23, function 6  -> /dev/i2c7
 *   SPI1  PD10..PD13, function 6 -> /dev/spi1
 *   PWM1 channel 9, PJ27 function 3 -> /dev/pwm0 (board fan)
 *
 * S-TWI0 (PMIC), S-TWI1 (Type-C), UART0 and PF0..PF6 (boot SD) are never
 * touched here.  Transfers are intentionally polling until the A733 GIC
 * peripheral interrupt paths have independent hardware acceptance tests.
 */

#include <nuttx/config.h>

#if defined(CONFIG_A733_HEADER_I2C) || defined(CONFIG_A733_HEADER_SPI) || \
    defined(CONFIG_A733_FAN_PWM)

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/clock.h>
#include <nuttx/mutex.h>

#include "arm64_internal.h"

#define A733_CCU_BASE          UINT64_C(0x02002000)
#define A733_PIO_BASE          UINT64_C(0x02000000)

/* A733 (sun60iw2) uses pinctrl HW type 4, NOT the legacy Allwinner layout:
 * bank A starts at +0x80, each bank occupies 0x80 bytes, drive registers start
 * at +0x20 and pull registers at +0x30.  a733_uart4.c documents the same thing
 * and is proven on hardware.  Using the legacy 0x30 stride here silently
 * programmed the wrong addresses, so the SPI1 pins were never muxed.
 */

/* Main PIO: sun60iw2 hw_type 4 */
#define A733_PIO_FIRST_BANK    UINT64_C(0x80)
#define A733_PIO_STRIDE        UINT64_C(0x80)
#define A733_PIO_DRV_OFF       UINT64_C(0x20)
#define A733_PIO_PUL_OFF       UINT64_C(0x30)
#define A733_PIO_DATA_OFF      UINT64_C(0x10)

#define A733_R_PIO_BASE        UINT64_C(0x07025000)
#define A733_R_CCU_BASE        UINT64_C(0x07010000)

/* R_PIO: sun60iw2 hw_type 1, taken from the vendor driver (NOT measured).
 *
 *   drivers/pinctrl/pinctrl-sun60iw2-r.c:  .hw_type = SUNXI_PCTL_HW_TYPE_1
 *   pinctrl-sunxi.c sunxi_pinctrl_hw_info[1]:
 *       mux 0x00, data 0x10, dlevel 0x14, bank_mem_size 0x30, pull 0x24,
 *       8 dlevel pins per reg * 4 bits
 *   pinctrl-sunxi.h sunxi_pinctrl_recalc_offset():
 *       bank * bank_mem_size + initial_bank_offset (initial_bank_offset = 0)
 *   pinctrl-sun60iw2-r.c sun60iw2_r_bank_base[]:
 *       { SUNXI_BANK_OFFSET('L','L'), SUNXI_BANK_OFFSET('M','L') }
 *
 * So bank L is the ORIGIN of the R_PIO block, i.e. +0x000, not +0x210.
 * An earlier revision of this file wrongly derived +0x210 / stride 0x104 by
 * multiplying the bank index (11) by the 0x30 stride in a debug script; every
 * write therefore landed 0x210 bytes too high and PL5 was never driven.
 *
 * Register offsets within bank L (PL5 -> word 0x04, bits 20..23):
 *   +0x00 mux pins  0..7      +0x04 mux pins 8..15
 *   +0x10 data (1 bit per pin, bit 5 = PL5)
 *   +0x14 dlevel pins 0..7 (4 bits each)   +0x18 dlevel pins 8..15
 *   +0x24 pull   pins 0..15 (2 bits each)
 */

#define A733_RPIO_FIRST_BANK   UINT64_C(0x000)  /* bank L is the base bank */
#define A733_RPIO_STRIDE       UINT64_C(0x30)   /* bank_mem_size for hw_type 1 */
#define A733_RPIO_MUX_OFF      UINT64_C(0x00)   /* + (pin / 8) * 4        */
#define A733_RPIO_DATA_OFF     UINT64_C(0x10)
#define A733_RPIO_DRV_OFF      UINT64_C(0x14)   /* + (pin / 8) * 4        */
#define A733_RPIO_PUL_OFF      UINT64_C(0x24)   /* + (pin / 16) * 4       */

/* Bank numbers are the numeric block index, not the letter: PA=0 .. PK=10. */

#define A733_BANK_PA           0u
#define A733_BANK_PB           1u
#define A733_BANK_PC           2u
#define A733_BANK_PD           3u
#define A733_BANK_PE           4u
#define A733_BANK_PF           5u
#define A733_BANK_PG           6u
#define A733_BANK_PH           7u
#define A733_BANK_PI           8u
#define A733_BANK_PJ           9u
#define A733_BANK_PK           10u
#define A733_BANK_PL           11u

static uintptr_t a733_bank_base(uintptr_t pio_base, unsigned int bank);

static uintptr_t a733_bank_base(uintptr_t pio_base, unsigned int bank)
{
  return pio_base + A733_PIO_FIRST_BANK + bank * A733_PIO_STRIDE;
}

static uintptr_t a733_rpio_bank_base(unsigned int bank)
{
  /* Bank L is the first bank of the R_PIO block (bank_base L->L), so its
   * offset is 0 and every later bank steps by bank_mem_size (0x30).
   */

  return A733_R_PIO_BASE + A733_RPIO_FIRST_BANK +
         (bank - A733_BANK_PL) * A733_RPIO_STRIDE;
}

/* Drive an already-configured R_PIO pin without touching mux/drive/pull. */

static void a733_rpio_write(unsigned int bank, unsigned int pin, bool high)
{
  uintptr_t base = a733_rpio_bank_base(bank);
  uint32_t value = getreg32(base + A733_RPIO_DATA_OFF);

  if (high)
    {
      value |= 1u << pin;
    }
  else
    {
      value &= ~(1u << pin);
    }

  putreg32(value, base + A733_RPIO_DATA_OFF);
}

/* Configure a pin of an R_PIO bank as a push-pull output and drive it. */

static void a733_rpio_output(unsigned int bank, unsigned int pin, bool high)
{
  uintptr_t base = a733_rpio_bank_base(bank);
  uintptr_t cfg = base + A733_RPIO_MUX_OFF + (pin / 8) * 4;
  uintptr_t drv = base + A733_RPIO_DRV_OFF + (pin / 8) * 4;
  uintptr_t pul = base + A733_RPIO_PUL_OFF + (pin / 16) * 4;
  unsigned int cfgshift = (pin % 8) * 4;
  unsigned int drvshift = (pin % 8) * 4;
  unsigned int pulshift = (pin % 16) * 2;
  uint32_t value;

  /* Function 1 is plain GPIO output on bank L. */

  value = getreg32(cfg);
  putreg32((value & ~(0xfu << cfgshift)) | (1u << cfgshift), cfg);

  /* 10 mA, matching the official board DTS. */

  value = getreg32(drv);
  putreg32((value & ~(0xfu << drvshift)) | (1u << drvshift), drv);

  value = getreg32(pul);
  putreg32(value & ~(3u << pulshift), pul);

  value = getreg32(base + A733_RPIO_DATA_OFF);
  if (high)
    {
      value |= 1u << pin;
    }
  else
    {
      value &= ~(1u << pin);
    }

  putreg32(value, base + A733_RPIO_DATA_OFF);
}

static void a733_pinmux(unsigned int bank, unsigned int pin,
                        unsigned int function, unsigned int pull)
{
  uintptr_t base = a733_bank_base(A733_PIO_BASE, bank);
  uintptr_t cfg = base + (pin / 8) * 4;
  uintptr_t drv = base + A733_PIO_DRV_OFF + (pin / 8) * 4;
  uintptr_t pul = base + A733_PIO_PUL_OFF + (pin / 16) * 4;
  unsigned int cfgshift = (pin % 8) * 4;
  unsigned int drvshift = (pin % 8) * 4;
  unsigned int pulshift = (pin % 16) * 2;
  uint32_t value;

  value = getreg32(cfg);
  putreg32((value & ~(0xfu << cfgshift)) | (function << cfgshift), cfg);

  /* 10 mA is the value used by the official board DTS. */

  value = getreg32(drv);
  putreg32((value & ~(0xfu << drvshift)) | (1u << drvshift), drv);

  value = getreg32(pul);
  putreg32((value & ~(3u << pulshift)) | (pull << pulshift), pul);
}

/* pio_base here is a BANK base, i.e. already passed through a733_bank_base().
 * a733_gpio_output() below takes the CONTROLLER base and the bank index, which
 * is what the ST7735 glue needs for PL5.
 */



#ifdef CONFIG_A733_HEADER_I2C

#include <nuttx/i2c/i2c_master.h>

#define TWI_DATA               0x08
#define TWI_CTL                0x0c
#define TWI_STAT               0x10
#define TWI_CLK                0x14
#define TWI_SRST               0x18

#define TWI_CTL_EN             (1u << 6)
#define TWI_CTL_START          (1u << 5)
#define TWI_CTL_STOP           (1u << 4)
#define TWI_CTL_IFLG           (1u << 3)
#define TWI_CTL_ACK            (1u << 2)

#define TWI_STAT_START         0x08
#define TWI_STAT_RSTART        0x10
#define TWI_STAT_AW_ACK        0x18
#define TWI_STAT_DW_ACK        0x28
#define TWI_STAT_AR_ACK        0x40
#define TWI_STAT_DR_ACK        0x50
#define TWI_STAT_DR_NACK       0x58

#define A733_TWI_TIMEOUT       MSEC2TICK(100)

struct a733_twi_s
{
  struct i2c_master_s dev;
  mutex_t lock;
  uintptr_t base;
  uintptr_t bgr;
  uint32_t frequency;
  uint8_t bus;
};

static int a733_twi_wait(struct a733_twi_s *priv)
{
  clock_t start = clock_systime_ticks();

  while ((getreg32(priv->base + TWI_CTL) & TWI_CTL_IFLG) == 0)
    {
      if (clock_systime_ticks() - start > A733_TWI_TIMEOUT)
        {
          return -ETIMEDOUT;
        }
    }

  return OK;
}

static void a733_twi_continue(struct a733_twi_s *priv, bool ack)
{
  putreg32(TWI_CTL_EN | TWI_CTL_IFLG | (ack ? TWI_CTL_ACK : 0),
           priv->base + TWI_CTL);
}

static int a733_twi_start(struct a733_twi_s *priv)
{
  uint32_t status;
  int ret;

  putreg32(TWI_CTL_EN | TWI_CTL_START, priv->base + TWI_CTL);
  ret = a733_twi_wait(priv);
  if (ret < 0)
    {
      return ret;
    }

  status = getreg32(priv->base + TWI_STAT) & 0xff;
  return status == TWI_STAT_START || status == TWI_STAT_RSTART ? OK : -EIO;
}

static void a733_twi_stop(struct a733_twi_s *priv)
{
  clock_t start;

  putreg32(TWI_CTL_EN | TWI_CTL_STOP | TWI_CTL_IFLG,
           priv->base + TWI_CTL);
  start = clock_systime_ticks();
  while ((getreg32(priv->base + TWI_CTL) & TWI_CTL_STOP) != 0 &&
         clock_systime_ticks() - start <= A733_TWI_TIMEOUT)
    {
    }
}

static void a733_twi_setclock(struct a733_twi_s *priv, uint32_t frequency)
{
  uint32_t best = 0;
  uint32_t bestfreq = 0;
  unsigned int n;
  unsigned int m;

  if (frequency == 0)
    {
      frequency = 100000;
    }

  if (frequency == priv->frequency)
    {
      return;
    }

  for (n = 0; n < 8; n++)
    {
      for (m = 0; m < 16; m++)
        {
          uint32_t actual = 24000000u / (10u * (1u << n) * (m + 1u));
          if (actual <= frequency && actual > bestfreq)
            {
              bestfreq = actual;
              best = (m << 3) | n;
            }
        }
    }

  putreg32(best, priv->base + TWI_CLK);
  priv->frequency = frequency;
}

static int a733_twi_transfer(struct i2c_master_s *dev,
                             struct i2c_msg_s *msgs, int count)
{
  struct a733_twi_s *priv = (struct a733_twi_s *)dev;
  uint32_t status;
  int ret;
  int mi;
  ssize_t bi;

  if (msgs == NULL || count <= 0)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  a733_twi_setclock(priv, msgs[0].frequency);
  ret = OK;

  for (mi = 0; mi < count && ret >= 0; mi++)
    {
      struct i2c_msg_s *msg = &msgs[mi];
      bool read = (msg->flags & I2C_M_READ) != 0;

      if ((msg->flags & I2C_M_TEN) != 0 || msg->length < 0)
        {
          ret = -ENOTSUP;
          break;
        }

      a733_twi_setclock(priv, msg->frequency);
      ret = a733_twi_start(priv);
      if (ret < 0)
        {
          break;
        }

      putreg32((msg->addr << 1) | (read ? 1u : 0u),
               priv->base + TWI_DATA);
      a733_twi_continue(priv, false);
      ret = a733_twi_wait(priv);
      if (ret < 0)
        {
          break;
        }

      status = getreg32(priv->base + TWI_STAT) & 0xff;
      if (status != (read ? TWI_STAT_AR_ACK : TWI_STAT_AW_ACK))
        {
          ret = status == 0x20 || status == 0x48 ? -ENXIO : -EIO;
          break;
        }

      if (read)
        {
          for (bi = 0; bi < msg->length; bi++)
            {
              bool ack = bi + 1 < msg->length;
              a733_twi_continue(priv, ack);
              ret = a733_twi_wait(priv);
              if (ret < 0)
                {
                  break;
                }

              status = getreg32(priv->base + TWI_STAT) & 0xff;
              if (status != (ack ? TWI_STAT_DR_ACK : TWI_STAT_DR_NACK))
                {
                  ret = -EIO;
                  break;
                }

              msg->buffer[bi] = getreg32(priv->base + TWI_DATA) & 0xff;
            }
        }
      else
        {
          for (bi = 0; bi < msg->length; bi++)
            {
              putreg32(msg->buffer[bi], priv->base + TWI_DATA);
              a733_twi_continue(priv, false);
              ret = a733_twi_wait(priv);
              if (ret < 0)
                {
                  break;
                }

              if ((getreg32(priv->base + TWI_STAT) & 0xff) !=
                  TWI_STAT_DW_ACK)
                {
                  ret = -EIO;
                  break;
                }
            }
        }
    }

  a733_twi_stop(priv);
  if (ret < 0)
    {
      putreg32(1, priv->base + TWI_SRST);
    }

  nxmutex_unlock(&priv->lock);
  return ret;
}

static int a733_twi_setup(struct i2c_master_s *dev)
{
  return OK;
}

static int a733_twi_shutdown(struct i2c_master_s *dev)
{
  return OK;
}

static const struct i2c_ops_s g_a733_twi_ops =
{
  .transfer = a733_twi_transfer,
  .setup = a733_twi_setup,
  .shutdown = a733_twi_shutdown,
};

static struct a733_twi_s g_a733_twi2 =
{
  .dev = { .ops = &g_a733_twi_ops },
  .lock = NXMUTEX_INITIALIZER,
  .base = UINT64_C(0x02512000),
  .bgr = A733_CCU_BASE + 0x0e88,
  .bus = 2,
};

static struct a733_twi_s g_a733_twi7 =
{
  .dev = { .ops = &g_a733_twi_ops },
  .lock = NXMUTEX_INITIALIZER,
  .base = UINT64_C(0x02517000),
  .bgr = A733_CCU_BASE + 0x0e9c,
  .bus = 7,
};

static int a733_twi_register(struct a733_twi_s *priv)
{
  putreg32((1u << 16) | 1u, priv->bgr);
  putreg32(1, priv->base + TWI_SRST);
  putreg32(TWI_CTL_EN, priv->base + TWI_CTL);
  a733_twi_setclock(priv, 100000);
  return i2c_register(&priv->dev, priv->bus);
}

static int a733_i2c_initialize(void)
{
  int first = OK;
  int ret;

  a733_pinmux(3, 16, 6, 1); /* PD16 TWI2_SCK, pull-up */
  a733_pinmux(3, 17, 6, 1); /* PD17 TWI2_SDA, pull-up */
  a733_pinmux(8, 22, 6, 1); /* PJ22 TWI7_SCK, pull-up */
  a733_pinmux(8, 23, 6, 1); /* PJ23 TWI7_SDA, pull-up */

  ret = a733_twi_register(&g_a733_twi2);
  if (ret < 0)
    {
      first = ret;
      syslog(LOG_ERR, "A733 TWI2: registration failed: %d\n", ret);
    }

  ret = a733_twi_register(&g_a733_twi7);
  if (ret < 0)
    {
      if (first == OK)
        {
          first = ret;
        }

      syslog(LOG_ERR, "A733 TWI7: registration failed: %d\n", ret);
    }

  return first;
}
#endif /* CONFIG_A733_HEADER_I2C */

#ifdef CONFIG_A733_HEADER_SPI

#include <nuttx/fs/fs.h>
#include <nuttx/spi/spi.h>
#include <nuttx/spi/spi_transfer.h>

#define A733_SPI1_BASE         UINT64_C(0x02541000)
#define SPI_GCR                0x04
#define SPI_TCR                0x08
#define SPI_ISR                0x14
#define SPI_FCR                0x18
#define SPI_FSR                0x1c
#define SPI_CCR                0x24
#define SPI_BC                 0x30
#define SPI_TC                 0x34
#define SPI_BCC                0x38
#define SPI_TXD                0x200
#define SPI_RXD                0x300

#define SPI_GCR_EN             (1u << 0)
#define SPI_GCR_MASTER         (1u << 1)
#define SPI_GCR_TP             (1u << 7)
#define SPI_GCR_SRST           (1u << 31)
#define SPI_TCR_CPHA           (1u << 0)
#define SPI_TCR_CPOL           (1u << 1)
#define SPI_TCR_SPOL           (1u << 2)
#define SPI_TCR_SS_OWNER       (1u << 6)
#define SPI_TCR_SS_LEVEL       (1u << 7)
#define SPI_TCR_XCH            (1u << 31)
#define SPI_FCR_RXRST          (1u << 15)
#define SPI_FCR_TXRST          (1u << 31)
#define SPI_ISR_TC             (1u << 12)

struct a733_spi_s
{
  struct spi_dev_s dev;
  mutex_t lock;
  uint32_t frequency;
  enum spi_mode_e mode;
  uint8_t nbits;
};

static bool g_a733_spi_ready;
static bool g_a733_spi_registered;

/* Byte order of 16-bit SPI words.  false = high byte first (the ST7735
 * datasheet order, and the driver default).  Exposed so the panel can be
 * tested both ways without a rebuild; see a733_spi_exchange().
 */

static bool g_a733_spi_lsb_word;

/* Called by the board glue through the diagnostic node. */

void a733_spi_set_word_lsb(bool lsb);

void a733_spi_set_word_lsb(bool lsb)
{
  g_a733_spi_lsb_word = lsb;
}

bool a733_spi_word_is_lsb(void);

bool a733_spi_word_is_lsb(void)
{
  return g_a733_spi_lsb_word;
}

static int a733_spi_lock(struct spi_dev_s *dev, bool lock)
{
  struct a733_spi_s *priv = (struct a733_spi_s *)dev;
  return lock ? nxmutex_lock(&priv->lock) : nxmutex_unlock(&priv->lock);
}

static void a733_spi_select(struct spi_dev_s *dev, uint32_t devid,
                            bool selected)
{
  uint32_t value = getreg32(A733_SPI1_BASE + SPI_TCR);

  value &= ~(3u << 4);
  value |= ((devid & 3u) << 4) | SPI_TCR_SS_OWNER | SPI_TCR_SPOL;
  if (selected)
    {
      value &= ~SPI_TCR_SS_LEVEL;
    }
  else
    {
      value |= SPI_TCR_SS_LEVEL;
    }

  putreg32(value, A733_SPI1_BASE + SPI_TCR);
}

static uint32_t a733_spi_setfrequency(struct spi_dev_s *dev,
                                      uint32_t frequency)
{
  struct a733_spi_s *priv = (struct a733_spi_s *)dev;
  uint32_t divider;

  if (frequency == 0)
    {
      frequency = 1000000;
    }

  if (frequency > 12000000)
    {
      frequency = 12000000;
    }

  divider = (24000000u + 2u * frequency - 1u) / (2u * frequency);
  divider = divider > 0 ? divider - 1 : 0;
  if (divider > 0xff)
    {
      divider = 0xff;
    }

  putreg32((1u << 12) | divider, A733_SPI1_BASE + SPI_CCR);
  priv->frequency = 24000000u / (2u * (divider + 1u));
  return priv->frequency;
}

static void a733_spi_setmode(struct spi_dev_s *dev, enum spi_mode_e mode)
{
  struct a733_spi_s *priv = (struct a733_spi_s *)dev;
  uint32_t value = getreg32(A733_SPI1_BASE + SPI_TCR);

  value &= ~(SPI_TCR_CPHA | SPI_TCR_CPOL);
  if (mode == SPIDEV_MODE1 || mode == SPIDEV_MODE3)
    {
      value |= SPI_TCR_CPHA;
    }

  if (mode == SPIDEV_MODE2 || mode == SPIDEV_MODE3)
    {
      value |= SPI_TCR_CPOL;
    }

  putreg32(value, A733_SPI1_BASE + SPI_TCR);
  priv->mode = mode;
}

static void a733_spi_setbits(struct spi_dev_s *dev, int nbits)
{
  struct a733_spi_s *priv = (struct a733_spi_s *)dev;
  priv->nbits = nbits == 16 ? 16 : 8;
}

static uint8_t a733_spi_status(struct spi_dev_s *dev, uint32_t devid)
{
  return SPI_STATUS_PRESENT;
}

#ifdef CONFIG_SPI_CMDDATA
static int a733_spi_cmddata(struct spi_dev_s *dev, uint32_t devid, bool cmd)
{
  (void)dev;

  if (devid != SPIDEV_DISPLAY(0))
    {
      return -ENODEV;
    }

  /* Cubie A7Z pin 22 is PL5 in the R_PIO domain.  ST7735 D/C is low for
   * commands and high for display data.
   */

  a733_rpio_write(A733_BANK_PL, 5, !cmd);
  return OK;
}
#endif

static void a733_spi_transfer_bytes(const uint8_t *tx, uint8_t *rx,
                                    size_t nbytes)
{
  while (nbytes > 0)
    {
      size_t chunk = nbytes > 63 ? 63 : nbytes;
      clock_t start;
      size_t i;

      putreg32(SPI_FCR_RXRST | SPI_FCR_TXRST, A733_SPI1_BASE + SPI_FCR);
      putreg32(SPI_ISR_TC, A733_SPI1_BASE + SPI_ISR);
      putreg32(chunk, A733_SPI1_BASE + SPI_BC);
      putreg32(chunk, A733_SPI1_BASE + SPI_TC);
      putreg32(chunk, A733_SPI1_BASE + SPI_BCC);

      for (i = 0; i < chunk; i++)
        {
          putreg32(tx != NULL ? tx[i] : 0xff,
                   A733_SPI1_BASE + SPI_TXD);
        }

      putreg32(getreg32(A733_SPI1_BASE + SPI_TCR) | SPI_TCR_XCH,
               A733_SPI1_BASE + SPI_TCR);
      start = clock_systime_ticks();
      while ((getreg32(A733_SPI1_BASE + SPI_FSR) & 0xffu) < chunk)
        {
          if (clock_systime_ticks() - start > MSEC2TICK(100))
            {
              syslog(LOG_ERR, "A733 SPI1: transfer timeout fsr=%08lx\n",
                     (unsigned long)getreg32(A733_SPI1_BASE + SPI_FSR));
              return;
            }
        }

      for (i = 0; i < chunk; i++)
        {
          uint8_t value = getreg32(A733_SPI1_BASE + SPI_RXD) & 0xff;
          if (rx != NULL)
            {
              rx[i] = value;
            }
        }

      if (tx != NULL)
        {
          tx += chunk;
        }

      if (rx != NULL)
        {
          rx += chunk;
        }

      nbytes -= chunk;
    }
}

static void a733_spi_exchange(struct spi_dev_s *dev, const void *txbuffer,
                              void *rxbuffer, size_t nwords)
{
  struct a733_spi_s *priv = (struct a733_spi_s *)dev;

  if (priv->nbits == 16)
    {
      const uint16_t *tx = txbuffer;
      uint16_t *rx = rxbuffer;
      uint8_t txbytes[62];
      uint8_t rxbytes[62];

      while (nwords > 0)
        {
          size_t chunk = nwords > 31 ? 31 : nwords;
          size_t i;

          for (i = 0; i < chunk; i++)
            {
              uint16_t word = tx != NULL ? tx[i] : 0xffff;

              /* Byte order of a 16-bit RGB565 word on the wire.
               *
               * The ST7735 datasheet says the high byte goes first, and that
               * is what the driver sends by default.  A panel that renders
               * fine structure (so the SPI link and the D/C line both work)
               * as vertical stripes instead of solid colour is misreading
               * those byte pairs, so this must be selectable at runtime:
               * diagnosing it one rebuild at a time is far too slow.
               */

              if (g_a733_spi_lsb_word)
                {
                  txbytes[2 * i] = word & 0xff;
                  txbytes[2 * i + 1] = word >> 8;
                }
              else
                {
                  txbytes[2 * i] = word >> 8;
                  txbytes[2 * i + 1] = word & 0xff;
                }
            }

          a733_spi_transfer_bytes(txbytes, rx != NULL ? rxbytes : NULL,
                                  chunk * 2);
          if (rx != NULL)
            {
              for (i = 0; i < chunk; i++)
                {
                  if (g_a733_spi_lsb_word)
                    {
                      rx[i] = ((uint16_t)rxbytes[2 * i + 1] << 8) |
                              rxbytes[2 * i];
                    }
                  else
                    {
                      rx[i] = ((uint16_t)rxbytes[2 * i] << 8) |
                              rxbytes[2 * i + 1];
                    }
                }

              rx += chunk;
            }

          if (tx != NULL)
            {
              tx += chunk;
            }

          nwords -= chunk;
        }
    }
  else
    {
      a733_spi_transfer_bytes(txbuffer, rxbuffer, nwords);
    }
}

static uint32_t a733_spi_send(struct spi_dev_s *dev, uint32_t word)
{
  struct a733_spi_s *priv = (struct a733_spi_s *)dev;

  if (priv->nbits == 16)
    {
      uint16_t tx = word;
      uint16_t rx = 0xffff;
      a733_spi_exchange(dev, &tx, &rx, 1);
      return rx;
    }
  else
    {
      uint8_t tx = word;
      uint8_t rx = 0xff;
      a733_spi_exchange(dev, &tx, &rx, 1);
      return rx;
    }
}

static const struct spi_ops_s g_a733_spi_ops =
{
  .lock = a733_spi_lock,
  .select = a733_spi_select,
  .setfrequency = a733_spi_setfrequency,
  .setmode = a733_spi_setmode,
  .setbits = a733_spi_setbits,
  .status = a733_spi_status,
#ifdef CONFIG_SPI_CMDDATA
  .cmddata = a733_spi_cmddata,
#endif
  .send = a733_spi_send,
#ifdef CONFIG_SPI_EXCHANGE
  .exchange = a733_spi_exchange,
#else
  .sndblock = (void *)a733_spi_exchange,
  .recvblock = (void *)a733_spi_exchange,
#endif
  .registercallback = NULL,
};

static struct a733_spi_s g_a733_spi1 =
{
  .dev = { .ops = &g_a733_spi_ops },
  .lock = NXMUTEX_INITIALIZER,
  .frequency = 1000000,
  .mode = SPIDEV_MODE0,
  .nbits = 8,
};

#if defined(CONFIG_BOARD_A7Z_ST7735) && defined(CONFIG_A733_HEADER_SPI)

/****************************************************************************
 * ST7735 electrical diagnostic node
 *
 * Read-only register/pad dump plus pin wiggle commands, mirroring the
 * /dev/a733-uart4 diagnostic idiom.  This exists because 'panel initialised
 * but the screen stays blank' cannot be told apart from 'D/C or CS is not
 * wired where we think' without reading the pads back.
 ****************************************************************************/

static ssize_t a733_lcd_diag_read(FAR struct file *filep, FAR char *buffer,
                                  size_t buflen)
{
  uintptr_t pio = a733_bank_base(A733_PIO_BASE, A733_BANK_PD);
  uintptr_t rpio = A733_R_PIO_BASE;
  char tmp[1024];
  int len;
  size_t offset;
  size_t copy;

  len = snprintf(tmp, sizeof(tmp),
    "A733 ST7735 electrical checkpoint\n"
    "pio-pd: cfg0=%08lx cfg1=%08lx drv0=%08lx pul0=%08lx data=%08lx\n"
    "rpio-pl: cfg0=%08lx drv0=%08lx pul0=%08lx data=%08lx pl5=%lu\n"
    "spi1: gcr=%08lx tcr=%08lx ccr=%08lx fsr=%08lx bc=%08lx tc=%08lx isr=%08lx\n"
    "ccu: spi1bgr=%08lx spi1clk=%08lx rpio=%08lx\n"
    "expect: PD10..13 mux=6, PL5 follows dcon/dcoff, gcr EN|MASTER set,\n"
    "        fsr free-count>0, tcr SS_OWNER|SPOL set\n",
    (unsigned long)getreg32(pio + 0x00),
    (unsigned long)getreg32(pio + 0x04),
    (unsigned long)getreg32(pio + 0x14),
    (unsigned long)getreg32(pio + 0x24),
    (unsigned long)getreg32(pio + 0x10),
    (unsigned long)getreg32(rpio + 0x00),
    (unsigned long)getreg32(rpio + 0x14),
    (unsigned long)getreg32(rpio + 0x24),
    (unsigned long)getreg32(rpio + 0x10),
    (unsigned long)((getreg32(rpio + 0x10) >> 5) & 1u),
    (unsigned long)getreg32(A733_SPI1_BASE + SPI_GCR),
    (unsigned long)getreg32(A733_SPI1_BASE + SPI_TCR),
    (unsigned long)getreg32(A733_SPI1_BASE + SPI_CCR),
    (unsigned long)getreg32(A733_SPI1_BASE + SPI_FSR),
    (unsigned long)getreg32(A733_SPI1_BASE + SPI_BC),
    (unsigned long)getreg32(A733_SPI1_BASE + SPI_TC),
    (unsigned long)getreg32(A733_SPI1_BASE + SPI_ISR),
    (unsigned long)getreg32(A733_CCU_BASE + 0x0f0c),
    (unsigned long)getreg32(A733_CCU_BASE + 0x0f08),
    (unsigned long)getreg32(A733_CCU_BASE + 0x1a0c));

  if (len < 0)
    {
      return -EIO;
    }

  /* Mirror a733_uart4.c: honour f_pos and return 0 at EOF, otherwise cat(1)
   * loops forever re-reading the same report.
   */

  offset = (size_t)filep->f_pos;
  if (offset >= (size_t)len)
    {
      return 0;
    }

  copy = (size_t)len - offset;
  if (copy > buflen)
    {
      copy = buflen;
    }

  memcpy(buffer, tmp + offset, copy);
  filep->f_pos += (off_t)copy;
  return (ssize_t)copy;
}

static ssize_t a733_lcd_diag_write(FAR struct file *filep,
                                   FAR const char *buffer, size_t buflen)
{
  char cmd[16];
  size_t n = buflen < sizeof(cmd) - 1 ? buflen : sizeof(cmd) - 1;

  memcpy(cmd, buffer, n);
  cmd[n] = '\0';

  while (n > 0 && (cmd[n - 1] == '\n' ||
                   cmd[n - 1] == '\r' ||
                   cmd[n - 1] == ' '))
    {
      cmd[--n] = '\0';
    }

  if (strcmp(cmd, "dcon") == 0)
    {
      a733_rpio_write(A733_BANK_PL, 5, true);
    }
  else if (strcmp(cmd, "dcoff") == 0)
    {
      a733_rpio_write(A733_BANK_PL, 5, false);
    }
  else if (strcmp(cmd, "selon") == 0)
    {
      uint32_t value = getreg32(A733_SPI1_BASE + SPI_TCR);
      putreg32(value & ~SPI_TCR_SS_LEVEL, A733_SPI1_BASE + SPI_TCR);
    }
  else if (strcmp(cmd, "seloff") == 0)
    {
      uint32_t value = getreg32(A733_SPI1_BASE + SPI_TCR);
      putreg32(value | SPI_TCR_SS_LEVEL, A733_SPI1_BASE + SPI_TCR);
    }
  else if (strcmp(cmd, "probe") == 0)
    {
      /* PL5 = Linux GPIO357 = gpiochip1 line 5, and it drives correctly from
       * Linux with this same wiring, so the pad and the D/C net are good.  Work
       * out which R_PIO register actually controls the output level by dumping
       * every candidate and testing write-ability with a distinctive pattern.
       */

      uintptr_t lbase = a733_rpio_bank_base(A733_BANK_PL);
      uintptr_t mux5 = lbase + A733_RPIO_MUX_OFF + (5u / 8u) * 4u;
      uintptr_t drv5 = lbase + A733_RPIO_DRV_OFF + (5u / 8u) * 4u;
      uintptr_t pul5 = lbase + A733_RPIO_PUL_OFF + (5u / 16u) * 4u;
      uint32_t saved = getreg32(lbase + A733_RPIO_DATA_OFF);
      uint32_t rb;

      syslog(LOG_INFO, "[a733-lcd] R_PIO bankL base=%08lx\n",
             (unsigned long)lbase);
      syslog(LOG_INFO, "[a733-lcd] bankL +00=%08lx +04=%08lx +10=%08lx "
             "+14=%08lx +18=%08lx +1c=%08lx +20=%08lx +24=%08lx +28=%08lx\n",
             (unsigned long)getreg32(lbase + 0x00),
             (unsigned long)getreg32(lbase + 0x04),
             (unsigned long)getreg32(lbase + 0x10),
             (unsigned long)getreg32(lbase + 0x14),
             (unsigned long)getreg32(lbase + 0x18),
             (unsigned long)getreg32(lbase + 0x1c),
             (unsigned long)getreg32(lbase + 0x20),
             (unsigned long)getreg32(lbase + 0x24),
             (unsigned long)getreg32(lbase + 0x28));

      /* R_CCU bus/clock registers for the R_PIO APB domain. */

      syslog(LOG_INFO, "[a733-lcd] r_ccu +00=%08lx +0c=%08lx +10=%08lx "
             "+1c=%08lx +20=%08lx\n",
             (unsigned long)getreg32(A733_R_CCU_BASE + 0x00),
             (unsigned long)getreg32(A733_R_CCU_BASE + 0x0c),
             (unsigned long)getreg32(A733_R_CCU_BASE + 0x10),
             (unsigned long)getreg32(A733_R_CCU_BASE + 0x1c),
             (unsigned long)getreg32(A733_R_CCU_BASE + 0x20));

      /* PL5 mux/dlevel/pull as programmed, so the pad setup is visible. */

      syslog(LOG_INFO, "[a733-lcd] PL5 mux@%08lx=%08lx drv@%08lx=%08lx "
             "pul@%08lx=%08lx\n",
             (unsigned long)mux5, (unsigned long)getreg32(mux5),
             (unsigned long)drv5, (unsigned long)getreg32(drv5),
             (unsigned long)pul5, (unsigned long)getreg32(pul5));

      /* Is the data register write-able?  Toggle it, read back, restore. */

      putreg32(0xffffffffu, lbase + A733_RPIO_DATA_OFF);
      syslog(LOG_INFO, "[a733-lcd] data <- ffffffff  readback=%08lx\n",
             (unsigned long)getreg32(lbase + A733_RPIO_DATA_OFF));
      putreg32(0x00000000u, lbase + A733_RPIO_DATA_OFF);
      syslog(LOG_INFO, "[a733-lcd] data <- 00000000  readback=%08lx\n",
             (unsigned long)getreg32(lbase + A733_RPIO_DATA_OFF));
      putreg32(saved, lbase + A733_RPIO_DATA_OFF);

      /* Also try the same offset on the M bank to see if L is special. */

      rb = a733_rpio_bank_base(12);
      syslog(LOG_INFO, "[a733-lcd] bankM base=%08lx +10=%08lx\n",
             (unsigned long)rb, (unsigned long)getreg32(rb + 0x10));

      /* Drive PL5 high and report the exact address and both reads. */

      putreg32(getreg32(lbase + A733_RPIO_DATA_OFF) | (1u << 5),
               lbase + A733_RPIO_DATA_OFF);
      syslog(LOG_INFO, "[a733-lcd] dcon mux@%08lx=%08lx drv@%08lx=%08lx "
             "data@%08lx=%08lx\n",
             (unsigned long)mux5,
             (unsigned long)getreg32(mux5),
             (unsigned long)drv5,
             (unsigned long)getreg32(drv5),
             (unsigned long)(lbase + A733_RPIO_DATA_OFF),
             (unsigned long)getreg32(lbase + A733_RPIO_DATA_OFF));
    }
  else if (strncmp(cmd, "pl5mux", 6) == 0)
    {
      uintptr_t lbase = a733_rpio_bank_base(A733_BANK_PL);
      uintptr_t cfg = lbase + A733_RPIO_MUX_OFF + (5u / 8u) * 4u;
      unsigned int fn = 1u;
      uint32_t value = getreg32(cfg);

      if (cmd[6] == ' ') { fn = (unsigned int)(cmd[7] - '0'); }
      putreg32((value & ~(0xfu << 20)) | (fn << 20), cfg);
      syslog(LOG_INFO, "[a733-lcd] pl5mux=%u cfg=%08lx -> %08lx\n", fn,
             (unsigned long)cfg, (unsigned long)getreg32(cfg));
    }
  else if (strcmp(cmd, "wordmsb") == 0 || strcmp(cmd, "wordlsb") == 0)
    {
      /* Flip the byte order of 16-bit SPI words without rebuilding, so a
       * striped panel can be told apart from a colour-order problem quickly.
       */

      bool lsb = cmd[4] == 'l';

      a733_spi_set_word_lsb(lsb);
      syslog(LOG_INFO, "[a733-lcd] 16-bit word order = %s\n",
             lsb ? "low byte first" : "high byte first");
    }
  else
    {
      return -EINVAL;
    }

  return (ssize_t)buflen;
}

static const struct file_operations g_a733_lcd_diag_fops =
{
  .read  = a733_lcd_diag_read,
  .write = a733_lcd_diag_write,
};

#endif /* CONFIG_BOARD_A7Z_ST7735 && CONFIG_A733_HEADER_SPI */

struct spi_dev_s *a733_spibus_initialize(int bus)
{
  unsigned int pin;

  if (bus != 1)
    {
      return NULL;
    }

  if (g_a733_spi_ready)
    {
      return &g_a733_spi1.dev;
    }

  for (pin = 10; pin <= 13; pin++)
    {
      a733_pinmux(3, pin, 6, pin == 10 ? 1 : 0);
    }

  putreg32((1u << 16) | 1u, A733_CCU_BASE + 0x0f0c);
  putreg32((1u << 31) | (7u << 24), A733_CCU_BASE + 0x0f08);
  putreg32(SPI_GCR_SRST, A733_SPI1_BASE + SPI_GCR);
  up_udelay(10);
  putreg32(SPI_GCR_EN | SPI_GCR_MASTER | SPI_GCR_TP,
           A733_SPI1_BASE + SPI_GCR);
  putreg32(SPI_TCR_SPOL | SPI_TCR_SS_OWNER | SPI_TCR_SS_LEVEL,
           A733_SPI1_BASE + SPI_TCR);
  a733_spi_setfrequency(&g_a733_spi1.dev, 1000000);

#ifdef CONFIG_BOARD_A7Z_ST7735
  /* Leave D/C in data mode before the generic ST7735 driver takes over. */

  a733_rpio_output(A733_BANK_PL, 5, true);

  /* Electrical diagnostic node: read the pads and controller registers
   * back when the panel initialises but the screen stays blank.
   */

  register_driver("/dev/a733-lcd", &g_a733_lcd_diag_fops, 0666, NULL);
#endif

  g_a733_spi_ready = true;
  return &g_a733_spi1.dev;
}

static int a733_spi_register(void)
{
  struct spi_dev_s *spi = a733_spibus_initialize(1);
  int ret;

  if (spi == NULL)
    {
      return -ENODEV;
    }

  if (g_a733_spi_registered)
    {
      return OK;
    }

  ret = spi_register(spi, 1);
  if (ret >= 0)
    {
      g_a733_spi_registered = true;
    }

  return ret;
}



#endif /* CONFIG_A733_HEADER_SPI */

#ifdef CONFIG_A733_FAN_PWM

#include <nuttx/timers/pwm.h>

#define A733_PWM1_BASE         UINT64_C(0x02528000)
#define PWM_PCCR89             0x30
#define PWM_PCGR               0x40
#define PWM_CER                0xc0
#define PWM_PCR9               (0x110 + 9 * 0x20)
#define PWM_PPR9               (0x118 + 9 * 0x20)

struct a733_pwm_s
{
  struct pwm_lowerhalf_s lower;
};

static int a733_pwm_setup(struct pwm_lowerhalf_s *dev)
{
  return OK;
}

static int a733_pwm_stop(struct pwm_lowerhalf_s *dev)
{
  putreg32(getreg32(A733_PWM1_BASE + PWM_CER) & ~(1u << 9),
           A733_PWM1_BASE + PWM_CER);
  return OK;
}

static int a733_pwm_shutdown(struct pwm_lowerhalf_s *dev)
{
  return a733_pwm_stop(dev);
}

static int a733_pwm_start(struct pwm_lowerhalf_s *dev,
                          const struct pwm_info_s *info)
{
  uint64_t cycles;
  uint64_t active;
  uint32_t pccr;

  if (info == NULL || info->frequency == 0)
    {
      return -EINVAL;
    }

  cycles = 24000000ull / info->frequency;
  if (cycles < 2 || cycles > 65536)
    {
      return -ERANGE;
    }

  active = (cycles * info->duty) >> 16;
  if (active > 65535)
    {
      active = 65535;
    }

  pccr = getreg32(A733_PWM1_BASE + PWM_PCCR89);
  pccr &= ~(0x1ffu << 16);
  pccr |= (1u << 20); /* channel 9 clock gate, HOSC, divider M=1 */
  putreg32(pccr, A733_PWM1_BASE + PWM_PCCR89);
  putreg32(1u << 8, A733_PWM1_BASE + PWM_PCR9);
  putreg32(((uint32_t)(cycles - 1) << 16) | (uint32_t)active,
           A733_PWM1_BASE + PWM_PPR9);
  putreg32(getreg32(A733_PWM1_BASE + PWM_PCGR) | (1u << 9),
           A733_PWM1_BASE + PWM_PCGR);
  putreg32(getreg32(A733_PWM1_BASE + PWM_CER) | (1u << 9),
           A733_PWM1_BASE + PWM_CER);
  return OK;
}

static int a733_pwm_ioctl(struct pwm_lowerhalf_s *dev, int cmd,
                          unsigned long arg)
{
  return -ENOTTY;
}

static const struct pwm_ops_s g_a733_pwm_ops =
{
  .setup = a733_pwm_setup,
  .shutdown = a733_pwm_shutdown,
  .start = a733_pwm_start,
  .stop = a733_pwm_stop,
  .ioctl = a733_pwm_ioctl,
};

static struct a733_pwm_s g_a733_fan_pwm =
{
  .lower = { .ops = &g_a733_pwm_ops },
};

static int a733_pwm_initialize(void)
{
  a733_pinmux(8, 27, 3, 0); /* PJ27 PWM1_CH9 */
  putreg32((1u << 16) | 1u, A733_CCU_BASE + 0x078c);
  a733_pwm_stop(&g_a733_fan_pwm.lower);
  return pwm_register("/dev/pwm0", &g_a733_fan_pwm.lower);
}
#endif /* CONFIG_A733_FAN_PWM */

int a733_header_peripherals_initialize(void)
{
  int first = OK;
  int ret;

#ifdef CONFIG_A733_HEADER_I2C
  ret = a733_i2c_initialize();
  if (ret < 0)
    {
      first = ret;
    }
#endif

#ifdef CONFIG_A733_HEADER_SPI
  ret = a733_spi_register();
  if (ret < 0)
    {
      if (first == OK)
        {
          first = ret;
        }

      syslog(LOG_ERR, "A733 SPI1: registration failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_A733_FAN_PWM
  ret = a733_pwm_initialize();
  if (ret < 0)
    {
      if (first == OK)
        {
          first = ret;
        }

      syslog(LOG_ERR, "A733 PWM1_CH9: registration failed: %d\n", ret);
    }
#endif

  return first;
}

#endif
