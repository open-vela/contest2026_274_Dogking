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
#define A733_PIO_FIRST_BANK    UINT64_C(0x80)
#define A733_PIO_STRIDE        UINT64_C(0x80)
#define A733_R_PIO_BASE        UINT64_C(0x07025000)

static void a733_pinmux(unsigned int bank, unsigned int pin,
                        unsigned int function, unsigned int pull)
{
  /* sun60iw2 main PIO uses hardware layout type 4.  Bank A starts at
   * +0x80, banks are 0x80 bytes apart, drive starts at +0x20 and pull at
   * +0x30.  R_PIO is a separate type-1 controller and is not handled by
   * this helper.
   */

  uintptr_t base = A733_PIO_BASE + A733_PIO_FIRST_BANK +
                   bank * A733_PIO_STRIDE;
  uintptr_t cfg = base + (pin / 8) * 4;
  uintptr_t drv = base + 0x20 + (pin / 8) * 4;
  uintptr_t pul = base + 0x30 + (pin / 16) * 4;
  unsigned int cfgshift = (pin % 8) * 4;
  unsigned int drvshift = (pin % 8) * 4;
  unsigned int pulshift = (pin % 16) * 2;
  uint32_t value;

  value = getreg32(cfg);
  value = (value & ~(0xfu << cfgshift)) | (function << cfgshift);
  putreg32(value, cfg);

  /* 10 mA is the value used by the official board DTS. */

  value = getreg32(drv);
  value = (value & ~(0xfu << drvshift)) | (1u << drvshift);
  putreg32(value, drv);

  value = getreg32(pul);
  value = (value & ~(3u << pulshift)) | (pull << pulshift);
  putreg32(value, pul);
}

static void a733_gpio_write(uintptr_t base, unsigned int pin, bool high)
{
  uint32_t value = getreg32(base + 0x10);

  if (high)
    {
      value |= 1u << pin;
    }
  else
    {
      value &= ~(1u << pin);
    }

  putreg32(value, base + 0x10);
}

static void a733_gpio_output(uintptr_t base, unsigned int pin, bool high)
{
  uintptr_t cfg = base + (pin / 8) * 4;
  unsigned int shift = (pin % 8) * 4;
  uint32_t value;

  a733_gpio_write(base, pin, high);
  value = getreg32(cfg);
  value = (value & ~(0xfu << shift)) | (1u << shift);
  putreg32(value, cfg);
}

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
#define SPI_TCR_DHB            (1u << 8)
#define SPI_TCR_XCH            (1u << 31)
#define SPI_FCR_RXRST          (1u << 15)
#define SPI_FCR_TXRST          (1u << 31)
#define SPI_ISR_ERROR_MASK     ((1u << 8) | (1u << 9) | (1u << 10))
#define SPI_ISR_TC             (1u << 12)

struct a733_spi_s
{
  struct spi_dev_s dev;
  mutex_t lock;
  uint32_t frequency;
  enum spi_mode_e mode;
  uint8_t nbits;
  int last_error;
};

static bool g_a733_spi_ready;
static bool g_a733_spi_registered;

static int a733_spi_lock(struct spi_dev_s *dev, bool lock)
{
  struct a733_spi_s *priv = (struct a733_spi_s *)dev;
  return lock ? nxmutex_lock(&priv->lock) : nxmutex_unlock(&priv->lock);
}

static void a733_spi_select(struct spi_dev_s *dev, uint32_t devid,
                            bool selected)
{
  uint32_t value = getreg32(A733_SPI1_BASE + SPI_TCR);

  (void)dev;

  value &= ~(3u << 4);
  value |= ((devid & 3u) << 4) | SPI_TCR_SS_OWNER | SPI_TCR_SPOL;

  /* SS_LEVEL is the literal software-controlled chip-select output level.
   * The ST7735 uses active-low CS, so assert by clearing the bit and release
   * by setting it.  This is also the exact polarity used by the only
   * hardware-tested A733 image that produced a visible panel response.
   */

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

  a733_gpio_write(A733_R_PIO_BASE, 5, !cmd);
  return OK;
}
#endif

static int a733_spi_transfer_bytes(const uint8_t *tx, uint8_t *rx,
                                   size_t nbytes)
{
  while (nbytes > 0)
    {
      size_t chunk = nbytes > 63 ? 63 : nbytes;
      clock_t start;
      uint32_t status;
      uint32_t tcr;
      size_t i;

      putreg32(SPI_FCR_RXRST | SPI_FCR_TXRST, A733_SPI1_BASE + SPI_FCR);
      putreg32(SPI_ISR_TC | SPI_ISR_ERROR_MASK,
               A733_SPI1_BASE + SPI_ISR);
      putreg32(chunk, A733_SPI1_BASE + SPI_BC);
      putreg32(chunk, A733_SPI1_BASE + SPI_TC);
      putreg32(chunk, A733_SPI1_BASE + SPI_BCC);

      /* The ST7735 is write-only.  Discard its unconnected MISO stream so a
       * full RX FIFO can never stall a long LVGL flush.  Clear DHB only for
       * genuine full-duplex callers.
       */

      tcr = getreg32(A733_SPI1_BASE + SPI_TCR);
      if (rx == NULL)
        {
          tcr |= SPI_TCR_DHB;
        }
      else
        {
          tcr &= ~SPI_TCR_DHB;
        }

      putreg32(tcr, A733_SPI1_BASE + SPI_TCR);

      for (i = 0; i < chunk; i++)
        {
          /* TXD/RXD are byte-wide FIFO apertures.  A 32-bit access is not
           * equivalent on this controller: it advances/fills multiple FIFO
           * lanes and caused ISR bit 10 (TX overflow) with a count of 67
           * while submitting a 63-byte chunk.  Match the vendor driver,
           * which uses writeb()/readb() for these two registers.
           */

          putreg8(tx != NULL ? tx[i] : 0xff,
                  A733_SPI1_BASE + SPI_TXD);
        }

      putreg32(tcr | SPI_TCR_XCH, A733_SPI1_BASE + SPI_TCR);
      start = clock_systime_ticks();
      do
        {
          status = getreg32(A733_SPI1_BASE + SPI_ISR);
          if ((status & SPI_ISR_ERROR_MASK) != 0)
            {
              syslog(LOG_ERR,
                     "A733 SPI1: transfer error isr=%08lx fsr=%08lx\n",
                     (unsigned long)status,
                     (unsigned long)getreg32(A733_SPI1_BASE + SPI_FSR));
              putreg32(status & (SPI_ISR_TC | SPI_ISR_ERROR_MASK),
                       A733_SPI1_BASE + SPI_ISR);
              return -EIO;
            }

          if (clock_systime_ticks() - start > MSEC2TICK(20))
            {
              syslog(LOG_ERR,
                     "A733 SPI1: transfer timeout isr=%08lx fsr=%08lx\n",
                     (unsigned long)status,
                     (unsigned long)getreg32(A733_SPI1_BASE + SPI_FSR));
              return -ETIMEDOUT;
            }
        }
      while ((status & SPI_ISR_TC) == 0);

      if (rx != NULL)
        {
          start = clock_systime_ticks();
          while ((getreg32(A733_SPI1_BASE + SPI_FSR) & 0xffu) < chunk)
            {
              if (clock_systime_ticks() - start > MSEC2TICK(20))
                {
                  syslog(LOG_ERR,
                         "A733 SPI1: RX timeout fsr=%08lx\n",
                         (unsigned long)getreg32(A733_SPI1_BASE + SPI_FSR));
                  return -ETIMEDOUT;
                }
            }

          for (i = 0; i < chunk; i++)
            {
              rx[i] = getreg8(A733_SPI1_BASE + SPI_RXD);
            }
        }

      putreg32(SPI_ISR_TC | SPI_ISR_ERROR_MASK,
               A733_SPI1_BASE + SPI_ISR);

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

  return OK;
}

static void a733_spi_exchange(struct spi_dev_s *dev, const void *txbuffer,
                              void *rxbuffer, size_t nwords)
{
  struct a733_spi_s *priv = (struct a733_spi_s *)dev;

  if (priv->last_error < 0)
    {
      return;
    }

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
              txbytes[2 * i] = word >> 8;
              txbytes[2 * i + 1] = word & 0xff;
            }

          priv->last_error =
            a733_spi_transfer_bytes(txbytes, rx != NULL ? rxbytes : NULL,
                                    chunk * 2);
          if (priv->last_error < 0)
            {
              return;
            }
          if (rx != NULL)
            {
              for (i = 0; i < chunk; i++)
                {
                  rx[i] = ((uint16_t)rxbytes[2 * i] << 8) |
                          rxbytes[2 * i + 1];
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
      priv->last_error = a733_spi_transfer_bytes(txbuffer, rxbuffer, nwords);
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
  .last_error = OK,
};

int a733_spibus_last_error(int bus)
{
  return bus == 1 ? g_a733_spi1.last_error : -ENODEV;
}

int a733_spibus_clear_error(int bus)
{
  if (bus != 1)
    {
      return -ENODEV;
    }

  putreg32(SPI_FCR_RXRST | SPI_FCR_TXRST, A733_SPI1_BASE + SPI_FCR);
  putreg32(SPI_ISR_TC | SPI_ISR_ERROR_MASK, A733_SPI1_BASE + SPI_ISR);
  g_a733_spi1.last_error = OK;
  return OK;
}

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

  a733_gpio_output(A733_R_PIO_BASE, 5, true);
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
