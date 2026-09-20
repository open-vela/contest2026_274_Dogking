/****************************************************************************
 * vendor/rockchip/chips/rk3506/rk3506_spi.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_RK3506_SPI0

#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/mutex.h>
#include <nuttx/spi/spi.h>

#include <arch/chip/rk3506_spi.h>

#include "arm_internal.h"

/* SPI0 uses the fixed M0 pin group from the RK3506 device tree:
 * GPIO0_C0 CLK, GPIO0_C1 MOSI, GPIO0_C2 MISO and GPIO0_C3 CS0.
 */

#define RK3506_SPI0_BASE              0xff120000u
#define RK3506_CRU_BASE               0xff9a0000u
#define RK3506_GPIO0_IOC_BASE         0xff950000u

#define RK3506_CRU_CLKSEL_CON34       (RK3506_CRU_BASE + 0x0388u)
#define RK3506_CRU_GATE_CON12         (RK3506_CRU_BASE + 0x0830u)
#define RK3506_GPIO0C_IOMUX_SEL_0     (RK3506_GPIO0_IOC_BASE + 0x0010u)
#define RK3506_GPIO0C_PULL            (RK3506_GPIO0_IOC_BASE + 0x0208u)
#define RK3506_GPIO0C_SMT             (RK3506_GPIO0_IOC_BASE + 0x0408u)

#define RK3506_WRITE_MASK(mask, val)  \
  (((uint32_t)(mask) << 16) | ((uint32_t)(val) & (mask)))

#define SPI_CTRLR0                    (RK3506_SPI0_BASE + 0x0000u)
#define SPI_CTRLR1                    (RK3506_SPI0_BASE + 0x0004u)
#define SPI_ENR                       (RK3506_SPI0_BASE + 0x0008u)
#define SPI_SER                       (RK3506_SPI0_BASE + 0x000cu)
#define SPI_BAUDR                     (RK3506_SPI0_BASE + 0x0010u)
#define SPI_TXFTLR                    (RK3506_SPI0_BASE + 0x0014u)
#define SPI_RXFTLR                    (RK3506_SPI0_BASE + 0x0018u)
#define SPI_TXFLR                     (RK3506_SPI0_BASE + 0x001cu)
#define SPI_RXFLR                     (RK3506_SPI0_BASE + 0x0020u)
#define SPI_SR                        (RK3506_SPI0_BASE + 0x0024u)
#define SPI_IMR                       (RK3506_SPI0_BASE + 0x002cu)
#define SPI_ICR                       (RK3506_SPI0_BASE + 0x0038u)
#define SPI_DMACR                     (RK3506_SPI0_BASE + 0x003cu)
#define SPI_TXDR                      (RK3506_SPI0_BASE + 0x0400u)
#define SPI_RXDR                      (RK3506_SPI0_BASE + 0x0800u)

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

struct rk3506_spidev_s
{
  struct spi_dev_s dev;
  mutex_t lock;
  uint32_t frequency;
  enum spi_mode_e mode;
  uint8_t nbits;
};

static int rk3506_spi_lock(FAR struct spi_dev_s *dev, bool lock);
static void rk3506_spi_select(FAR struct spi_dev_s *dev, uint32_t devid,
                              bool selected);
static uint32_t rk3506_spi_setfrequency(FAR struct spi_dev_s *dev,
                                        uint32_t frequency);
static void rk3506_spi_setmode(FAR struct spi_dev_s *dev,
                               enum spi_mode_e mode);
static void rk3506_spi_setbits(FAR struct spi_dev_s *dev, int nbits);
static uint8_t rk3506_spi_status(FAR struct spi_dev_s *dev,
                                 uint32_t devid);
static uint32_t rk3506_spi_send(FAR struct spi_dev_s *dev, uint32_t wd);
#ifdef CONFIG_SPI_EXCHANGE
static void rk3506_spi_exchange(FAR struct spi_dev_s *dev,
                                FAR const void *txbuffer,
                                FAR void *rxbuffer, size_t nwords);
#else
static void rk3506_spi_sndblock(FAR struct spi_dev_s *dev,
                                FAR const void *buffer, size_t nwords);
static void rk3506_spi_recvblock(FAR struct spi_dev_s *dev,
                                 FAR void *buffer, size_t nwords);
#endif

static const struct spi_ops_s g_spiops =
{
  .lock = rk3506_spi_lock,
  .select = rk3506_spi_select,
  .setfrequency = rk3506_spi_setfrequency,
  .setmode = rk3506_spi_setmode,
  .setbits = rk3506_spi_setbits,
  .status = rk3506_spi_status,
  .send = rk3506_spi_send,
#ifdef CONFIG_SPI_EXCHANGE
  .exchange = rk3506_spi_exchange,
#else
  .sndblock = rk3506_spi_sndblock,
  .recvblock = rk3506_spi_recvblock,
#endif
  .registercallback = NULL,
};

static struct rk3506_spidev_s g_spi0 =
{
  .dev = { .ops = &g_spiops },
  .lock = NXMUTEX_INITIALIZER,
  .frequency = SPI_DEFAULT_FREQUENCY,
  .mode = SPIDEV_MODE0,
  .nbits = 8,
};

static void rk3506_spi_configure(FAR struct rk3506_spidev_s *priv)
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

#ifdef CONFIG_RK3506_SPI0_LOOPBACK
  cr0 |= SPI_CR0_LOOPBACK;
#endif

  putreg32(0, SPI_ENR);
  putreg32(cr0, SPI_CTRLR0);
}

static int rk3506_spi_lock(FAR struct spi_dev_s *dev, bool lock)
{
  FAR struct rk3506_spidev_s *priv =
    (FAR struct rk3506_spidev_s *)dev;

  return lock ? nxmutex_lock(&priv->lock) : nxmutex_unlock(&priv->lock);
}

static void rk3506_spi_select(FAR struct spi_dev_s *dev, uint32_t devid,
                              bool selected)
{
  (void)dev;
  (void)devid;
  putreg32(selected ? 1u : 0u, SPI_SER);
}

static uint32_t rk3506_spi_setfrequency(FAR struct spi_dev_s *dev,
                                        uint32_t frequency)
{
  FAR struct rk3506_spidev_s *priv =
    (FAR struct rk3506_spidev_s *)dev;
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

static void rk3506_spi_setmode(FAR struct spi_dev_s *dev,
                               enum spi_mode_e mode)
{
  FAR struct rk3506_spidev_s *priv =
    (FAR struct rk3506_spidev_s *)dev;

  if (mode <= SPIDEV_MODE3 && priv->mode != mode)
    {
      priv->mode = mode;
      rk3506_spi_configure(priv);
    }
}

static void rk3506_spi_setbits(FAR struct spi_dev_s *dev, int nbits)
{
  FAR struct rk3506_spidev_s *priv =
    (FAR struct rk3506_spidev_s *)dev;

  if ((nbits == 8 || nbits == 16) && priv->nbits != nbits)
    {
      priv->nbits = nbits;
      rk3506_spi_configure(priv);
    }
}

static uint8_t rk3506_spi_status(FAR struct spi_dev_s *dev,
                                 uint32_t devid)
{
  (void)dev;
  (void)devid;
  return 0;
}

static void rk3506_spi_doexchange(FAR struct rk3506_spidev_s *priv,
                                  FAR const void *txbuffer,
                                  FAR void *rxbuffer, size_t nwords)
{
  size_t txwords = 0;
  size_t rxwords = 0;
  clock_t start;
  clock_t timeout;
  uint32_t word;

  rk3506_spi_configure(priv);
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
          syslog(LOG_ERR, "ERROR: SPI0 transfer timeout (%zu/%zu words)\n",
                 rxwords, nwords);
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

static uint32_t rk3506_spi_send(FAR struct spi_dev_s *dev, uint32_t wd)
{
  FAR struct rk3506_spidev_s *priv =
    (FAR struct rk3506_spidev_s *)dev;
  uint16_t tx = (uint16_t)wd;
  uint16_t rx = 0;

  rk3506_spi_doexchange(priv, &tx, &rx, 1);
  return priv->nbits == 16 ? rx : (uint8_t)rx;
}

#ifdef CONFIG_SPI_EXCHANGE
static void rk3506_spi_exchange(FAR struct spi_dev_s *dev,
                                FAR const void *txbuffer,
                                FAR void *rxbuffer, size_t nwords)
{
  rk3506_spi_doexchange((FAR struct rk3506_spidev_s *)dev,
                        txbuffer, rxbuffer, nwords);
}
#else
static void rk3506_spi_sndblock(FAR struct spi_dev_s *dev,
                                FAR const void *buffer, size_t nwords)
{
  rk3506_spi_doexchange((FAR struct rk3506_spidev_s *)dev,
                        buffer, NULL, nwords);
}

static void rk3506_spi_recvblock(FAR struct spi_dev_s *dev,
                                 FAR void *buffer, size_t nwords)
{
  rk3506_spi_doexchange((FAR struct rk3506_spidev_s *)dev,
                        NULL, buffer, nwords);
}
#endif

FAR struct spi_dev_s *rk3506_spibus_initialize(int bus)
{
  if (bus != 0)
    {
      return NULL;
    }

  /* Select the 24 MHz oscillator with divider one, then ungate SPI0's
   * functional and APB clocks.  Rockchip clock gates use zero for enabled.
   */

  putreg32(RK3506_WRITE_MASK(0x03f0u, 0), RK3506_CRU_CLKSEL_CON34);
  putreg32(RK3506_WRITE_MASK((1u << 10) | (1u << 11), 0),
           RK3506_CRU_GATE_CON12);

  /* Fixed M0 pinmux function 2; no pulls, Schmitt input enabled. */

  putreg32(RK3506_WRITE_MASK(0xffffu, 0x2222u),
           RK3506_GPIO0C_IOMUX_SEL_0);
  putreg32(RK3506_WRITE_MASK(0x00ffu, 0), RK3506_GPIO0C_PULL);
  putreg32(RK3506_WRITE_MASK(0x000fu, 0x000fu), RK3506_GPIO0C_SMT);

  putreg32(0, SPI_ENR);
  putreg32(0, SPI_SER);
  putreg32(0, SPI_IMR);
  putreg32(1, SPI_ICR);
  putreg32(0, SPI_DMACR);
  putreg32(0, SPI_CTRLR1);
  putreg32(SPI_FIFO_DEPTH / 2 - 1, SPI_TXFTLR);
  putreg32(SPI_FIFO_DEPTH / 2 - 1, SPI_RXFTLR);
  rk3506_spi_configure(&g_spi0);
  rk3506_spi_setfrequency(&g_spi0.dev, SPI_DEFAULT_FREQUENCY);
  return &g_spi0.dev;
}

#endif /* CONFIG_RK3506_SPI0 */
