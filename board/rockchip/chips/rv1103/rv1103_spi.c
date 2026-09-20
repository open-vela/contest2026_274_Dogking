/****************************************************************************
 * vendor/rockchip/chips/rv1103/rv1103_spi.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_RV1103_SPI

#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/mutex.h>
#include <nuttx/spi/spi.h>

#include <arch/chip/rv1103_periph.h>

#include "arm_internal.h"

/* DesignWare-derived Rockchip SPI controller. */

#define SPI_CTRLR0                    (priv->base + 0x0000u)
#define SPI_CTRLR1                    (priv->base + 0x0004u)
#define SPI_ENR                       (priv->base + 0x0008u)
#define SPI_SER                       (priv->base + 0x000cu)
#define SPI_BAUDR                     (priv->base + 0x0010u)
#define SPI_TXFTLR                    (priv->base + 0x0014u)
#define SPI_RXFTLR                    (priv->base + 0x0018u)
#define SPI_TXFLR                     (priv->base + 0x001cu)
#define SPI_RXFLR                     (priv->base + 0x0020u)
#define SPI_SR                        (priv->base + 0x0024u)
#define SPI_IMR                       (priv->base + 0x002cu)
#define SPI_ICR                       (priv->base + 0x0038u)
#define SPI_DMACR                     (priv->base + 0x003cu)
#define SPI_TXDR                      (priv->base + 0x0400u)
#define SPI_RXDR                      (priv->base + 0x0800u)

#define SPI_CR0_DFS_8                 (1u << 0)
#define SPI_CR0_DFS_16                (2u << 0)
#define SPI_CR0_CPHA                  (1u << 6)
#define SPI_CR0_CPOL                  (1u << 7)
#define SPI_CR0_SSD_ONE               (1u << 10)
#define SPI_CR0_ENDIAN_BIG            (1u << 11)
#define SPI_CR0_APB_8BIT              (1u << 13)
#define SPI_CR0_LOOPBACK              (1u << 25)

#define SPI_SR_BUSY                   (1u << 0)
#define SPI_FIFO_DEPTH                64u
#define SPI_INPUT_CLOCK               24000000u
#define SPI_DEFAULT_FREQUENCY         1000000u

struct rv1103_spidev_s
{
  struct spi_dev_s dev;
  uintptr_t base;
  uint8_t bus;
  mutex_t lock;
  uint32_t frequency;
  enum spi_mode_e mode;
  uint8_t nbits;
};

static int rv1103_spi_lock(FAR struct spi_dev_s *dev, bool lock);
static void rv1103_spi_select(FAR struct spi_dev_s *dev, uint32_t devid,
                              bool selected);
static uint32_t rv1103_spi_setfrequency(FAR struct spi_dev_s *dev,
                                        uint32_t frequency);
static void rv1103_spi_setmode(FAR struct spi_dev_s *dev,
                               enum spi_mode_e mode);
static void rv1103_spi_setbits(FAR struct spi_dev_s *dev, int nbits);
static uint8_t rv1103_spi_status(FAR struct spi_dev_s *dev,
                                 uint32_t devid);
static uint32_t rv1103_spi_send(FAR struct spi_dev_s *dev, uint32_t wd);
#ifdef CONFIG_SPI_EXCHANGE
static void rv1103_spi_exchange(FAR struct spi_dev_s *dev,
                                FAR const void *txbuffer,
                                FAR void *rxbuffer, size_t nwords);
#else
static void rv1103_spi_sndblock(FAR struct spi_dev_s *dev,
                                FAR const void *buffer, size_t nwords);
static void rv1103_spi_recvblock(FAR struct spi_dev_s *dev,
                                 FAR void *buffer, size_t nwords);
#endif

static const struct spi_ops_s g_spiops =
{
  .lock = rv1103_spi_lock,
  .select = rv1103_spi_select,
  .setfrequency = rv1103_spi_setfrequency,
  .setmode = rv1103_spi_setmode,
  .setbits = rv1103_spi_setbits,
  .status = rv1103_spi_status,
  .send = rv1103_spi_send,
#ifdef CONFIG_SPI_EXCHANGE
  .exchange = rv1103_spi_exchange,
#else
  .sndblock = rv1103_spi_sndblock,
  .recvblock = rv1103_spi_recvblock,
#endif
  .registercallback = NULL,
};

static struct rv1103_spidev_s g_spi0 =
{
  .dev = { .ops = &g_spiops },
  .lock = NXMUTEX_INITIALIZER,
  .base = 0xff500000u,
  .bus = 0,
  .frequency = SPI_DEFAULT_FREQUENCY,
  .mode = SPIDEV_MODE0,
  .nbits = 8,
};

static struct rv1103_spidev_s g_spi1 =
{
  .dev = { .ops = &g_spiops },
  .lock = NXMUTEX_INITIALIZER,
  .base = 0xff510000u,
  .bus = 1,
  .frequency = SPI_DEFAULT_FREQUENCY,
  .mode = SPIDEV_MODE0,
  .nbits = 8,
};

static void rv1103_spi_configure(FAR struct rv1103_spidev_s *priv)
{
  uint32_t cr0 = SPI_CR0_SSD_ONE | SPI_CR0_ENDIAN_BIG |
                 SPI_CR0_APB_8BIT;

  cr0 |= priv->nbits == 16 ? SPI_CR0_DFS_16 : SPI_CR0_DFS_8;
  if (priv->mode == SPIDEV_MODE1 || priv->mode == SPIDEV_MODE3)
    {
      cr0 |= SPI_CR0_CPHA;
    }

  if (priv->mode == SPIDEV_MODE2 || priv->mode == SPIDEV_MODE3)
    {
      cr0 |= SPI_CR0_CPOL;
    }

#ifdef CONFIG_RV1103_SPI_LOOPBACK
  cr0 |= SPI_CR0_LOOPBACK;
#endif

  putreg32(0, SPI_ENR);
  putreg32(cr0, SPI_CTRLR0);
}

static int rv1103_spi_lock(FAR struct spi_dev_s *dev, bool lock)
{
  FAR struct rv1103_spidev_s *priv =
    (FAR struct rv1103_spidev_s *)dev;

  return lock ? nxmutex_lock(&priv->lock) : nxmutex_unlock(&priv->lock);
}

static void rv1103_spi_select(FAR struct spi_dev_s *dev, uint32_t devid,
                              bool selected)
{
  FAR struct rv1103_spidev_s *priv =
    (FAR struct rv1103_spidev_s *)dev;

  (void)devid;
  putreg32(selected ? 1u : 0u, SPI_SER);
}

static uint32_t rv1103_spi_setfrequency(FAR struct spi_dev_s *dev,
                                        uint32_t frequency)
{
  FAR struct rv1103_spidev_s *priv =
    (FAR struct rv1103_spidev_s *)dev;
  uint32_t divider;

  if (frequency == 0)
    {
      frequency = SPI_DEFAULT_FREQUENCY;
    }

  divider = (SPI_INPUT_CLOCK + frequency - 1) / frequency;
  divider = (divider + 1) & ~1u;
  if (divider < 2)
    {
      divider = 2;
    }
  else if (divider > 0xfffe)
    {
      divider = 0xfffe;
    }

  putreg32(0, SPI_ENR);
  putreg32(divider, SPI_BAUDR);
  priv->frequency = SPI_INPUT_CLOCK / divider;
  return priv->frequency;
}

static void rv1103_spi_setmode(FAR struct spi_dev_s *dev,
                               enum spi_mode_e mode)
{
  FAR struct rv1103_spidev_s *priv =
    (FAR struct rv1103_spidev_s *)dev;

  if (mode <= SPIDEV_MODE3 && priv->mode != mode)
    {
      priv->mode = mode;
      rv1103_spi_configure(priv);
    }
}

static void rv1103_spi_setbits(FAR struct spi_dev_s *dev, int nbits)
{
  FAR struct rv1103_spidev_s *priv =
    (FAR struct rv1103_spidev_s *)dev;

  if ((nbits == 8 || nbits == 16) && priv->nbits != nbits)
    {
      priv->nbits = nbits;
      rv1103_spi_configure(priv);
    }
}

static uint8_t rv1103_spi_status(FAR struct spi_dev_s *dev,
                                 uint32_t devid)
{
  (void)dev;
  (void)devid;
  return 0;
}

static void rv1103_spi_doexchange(FAR struct rv1103_spidev_s *priv,
                                  FAR const void *txbuffer,
                                  FAR void *rxbuffer, size_t nwords)
{
  size_t txwords = 0;
  size_t rxwords = 0;
  clock_t start;
  clock_t timeout;
  uint32_t word;

  rv1103_spi_configure(priv);
  while (getreg32(SPI_RXFLR) != 0)
    {
      (void)getreg32(SPI_RXDR);
    }

  putreg32(1, SPI_ENR);
  start = clock_systime_ticks();
  timeout = MSEC2TICK(100 +
            (nwords * priv->nbits * 4000u) / priv->frequency);

  while (rxwords < nwords)
    {
      while (txwords < nwords &&
             txwords - rxwords < SPI_FIFO_DEPTH &&
             getreg32(SPI_TXFLR) < SPI_FIFO_DEPTH)
        {
          if (txbuffer == NULL)
            {
              word = priv->nbits == 16 ? 0xffffu : 0xffu;
            }
          else if (priv->nbits == 16)
            {
              word = ((FAR const uint16_t *)txbuffer)[txwords];
            }
          else
            {
              word = ((FAR const uint8_t *)txbuffer)[txwords];
            }

          putreg32(word, SPI_TXDR);
          txwords++;
        }

      while (rxwords < nwords && getreg32(SPI_RXFLR) != 0)
        {
          word = getreg32(SPI_RXDR);
          if (rxbuffer != NULL)
            {
              if (priv->nbits == 16)
                {
                  ((FAR uint16_t *)rxbuffer)[rxwords] = (uint16_t)word;
                }
              else
                {
                  ((FAR uint8_t *)rxbuffer)[rxwords] = (uint8_t)word;
                }
            }

          rxwords++;
        }

      if (clock_systime_ticks() - start > timeout)
        {
          syslog(LOG_ERR, "ERROR: SPI%d transfer timeout (%zu/%zu words)\n",
                 priv->bus, rxwords, nwords);
          break;
        }
    }

  start = clock_systime_ticks();
  while ((getreg32(SPI_SR) & SPI_SR_BUSY) != 0 &&
         clock_systime_ticks() - start <= MSEC2TICK(100))
    {
    }

  putreg32(0, SPI_ENR);
}

static uint32_t rv1103_spi_send(FAR struct spi_dev_s *dev, uint32_t wd)
{
  FAR struct rv1103_spidev_s *priv =
    (FAR struct rv1103_spidev_s *)dev;
  uint16_t tx = (uint16_t)wd;
  uint16_t rx = 0;

  rv1103_spi_doexchange(priv, &tx, &rx, 1);
  return priv->nbits == 16 ? rx : (uint8_t)rx;
}

#ifdef CONFIG_SPI_EXCHANGE
static void rv1103_spi_exchange(FAR struct spi_dev_s *dev,
                                FAR const void *txbuffer,
                                FAR void *rxbuffer, size_t nwords)
{
  rv1103_spi_doexchange((FAR struct rv1103_spidev_s *)dev,
                        txbuffer, rxbuffer, nwords);
}
#else
static void rv1103_spi_sndblock(FAR struct spi_dev_s *dev,
                                FAR const void *buffer, size_t nwords)
{
  rv1103_spi_doexchange((FAR struct rv1103_spidev_s *)dev,
                        buffer, NULL, nwords);
}

static void rv1103_spi_recvblock(FAR struct spi_dev_s *dev,
                                 FAR void *buffer, size_t nwords)
{
  rv1103_spi_doexchange((FAR struct rv1103_spidev_s *)dev,
                        NULL, buffer, nwords);
}
#endif

FAR struct spi_dev_s *rv1103_spibus_initialize(int bus)
{
  FAR struct rv1103_spidev_s *priv;

  if (bus == 0)
    {
      priv = &g_spi0;
      rv1103_pinctrl_config(1, 17, 4, RV1103_PULL_NONE, true); /* CLK */
      rv1103_pinctrl_config(1, 19, 6, RV1103_PULL_NONE, true); /* MISO */
      rv1103_pinctrl_config(1, 18, 6, RV1103_PULL_NONE, true); /* MOSI */
      rv1103_pinctrl_config(1, 16, 4, RV1103_PULL_UP, true);   /* CS0 */
    }
  else if (bus == 1)
    {
      priv = &g_spi1;
      rv1103_pinctrl_config(4, 7, 2, RV1103_PULL_NONE, true); /* CLK */
      rv1103_pinctrl_config(4, 0, 2, RV1103_PULL_NONE, true); /* MISO */
      rv1103_pinctrl_config(4, 1, 2, RV1103_PULL_NONE, true); /* MOSI */
      rv1103_pinctrl_config(4, 5, 2, RV1103_PULL_UP, true);   /* CS0 */
    }
  else
    {
      return NULL;
    }

  rv1103_periph_clock_spi(bus);
  putreg32(0, SPI_ENR);
  putreg32(0, SPI_SER);
  putreg32(0, SPI_IMR);
  putreg32(1, SPI_ICR);
  putreg32(0, SPI_DMACR);
  putreg32(0, SPI_CTRLR1);
  putreg32(SPI_FIFO_DEPTH / 2 - 1, SPI_TXFTLR);
  putreg32(SPI_FIFO_DEPTH / 2 - 1, SPI_RXFTLR);
  rv1103_spi_configure(priv);
  rv1103_spi_setfrequency(&priv->dev, SPI_DEFAULT_FREQUENCY);
  return &priv->dev;
}

#endif /* CONFIG_RV1103_SPI */
