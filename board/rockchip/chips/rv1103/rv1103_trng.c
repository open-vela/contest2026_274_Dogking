/****************************************************************************
 * vendor/rockchip/chips/rv1103/rv1103_trng.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_RV1103_TRNG

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/drivers/drivers.h>
#include <nuttx/fs/fs.h>
#include <nuttx/mutex.h>

#include "arm_internal.h"

/* RV1103 uses Rockchip's standalone non-secure TRNG v1 block.  The register
 * protocol and 0x46bc version value come from the official rockchip-rng.c.
 */

#define RV1103_TRNG_BASE               0xff448000u
#define RV1103_TRNG_CTRL               (RV1103_TRNG_BASE + 0x0000u)
#define RV1103_TRNG_STAT               (RV1103_TRNG_BASE + 0x0004u)
#define RV1103_TRNG_MODE               (RV1103_TRNG_BASE + 0x0008u)
#define RV1103_TRNG_ISTAT              (RV1103_TRNG_BASE + 0x0014u)
#define RV1103_TRNG_RAND0              (RV1103_TRNG_BASE + 0x0020u)
#define RV1103_TRNG_AUTO_RQSTS         (RV1103_TRNG_BASE + 0x0060u)
#define RV1103_TRNG_VERSION            (RV1103_TRNG_BASE + 0x00f0u)

#define RV1103_TRNG_CTRL_NOP           0u
#define RV1103_TRNG_CTRL_RAND          1u
#define RV1103_TRNG_MODE_256BIT        (1u << 3)
#define RV1103_TRNG_STAT_SEEDED        (1u << 9)
#define RV1103_TRNG_STAT_GENERATING    (1u << 30)
#define RV1103_TRNG_STAT_RESEEDING     (1u << 31)
#define RV1103_TRNG_ISTAT_RAND_RDY     (1u << 0)
#define RV1103_TRNG_VERSION_CODE       0x46bcu
#define RV1103_TRNG_BLOCK_SIZE         32u
#define RV1103_TRNG_TIMEOUT_US         50000u

/* HCLK_TRNG_NS is PERI CLKGATE_CON03 bit 9.  Its matching reset is
 * PERISOFTRST_CON03 bit 9.
 */

#define RV1103_PERI_CLKGATE_CON3       0xff3b280cu
#define RV1103_PERI_SOFTRST_CON3       0xff3b2a0cu
#define RV1103_TRNG_CLOCK_BIT          (1u << 9)
#define RV1103_TRNG_RESET_BIT          (1u << 9)

struct rv1103_trng_dev_s
{
  mutex_t lock;
  uint32_t last[RV1103_TRNG_BLOCK_SIZE / sizeof(uint32_t)];
  bool initialized;
  bool have_last;
};

static ssize_t rv1103_trng_read(struct file *filep, char *buffer,
                                size_t buflen);

static const struct file_operations g_rv1103_trng_fops =
{
  .read = rv1103_trng_read,
};

static struct rv1103_trng_dev_s g_rv1103_trng =
{
  .lock = NXMUTEX_INITIALIZER,
};

static int rv1103_trng_wait(uint32_t mask, uint32_t expected)
{
  unsigned int elapsed;

  for (elapsed = 0; elapsed < RV1103_TRNG_TIMEOUT_US; elapsed += 100)
    {
      if ((getreg32(RV1103_TRNG_ISTAT) & mask) == expected)
        {
          return 0;
        }

      up_udelay(100);
    }

  return -ETIMEDOUT;
}

static int rv1103_trng_wait_seeded(void)
{
  uint32_t mask = RV1103_TRNG_STAT_SEEDED |
                  RV1103_TRNG_STAT_GENERATING |
                  RV1103_TRNG_STAT_RESEEDING;
  unsigned int elapsed;

  for (elapsed = 0; elapsed < RV1103_TRNG_TIMEOUT_US; elapsed += 100)
    {
      if ((getreg32(RV1103_TRNG_STAT) & mask) ==
          RV1103_TRNG_STAT_SEEDED)
        {
          return 0;
        }

      up_udelay(100);
    }

  return -ETIMEDOUT;
}

static int rv1103_trng_generate(uint32_t output[8])
{
  uint32_t istat;
  uint32_t value;
  bool nonzero = false;
  unsigned int i;
  int ret;

  istat = getreg32(RV1103_TRNG_ISTAT);
  putreg32(istat, RV1103_TRNG_ISTAT);
  putreg32(RV1103_TRNG_MODE_256BIT, RV1103_TRNG_MODE);
  putreg32(RV1103_TRNG_CTRL_RAND, RV1103_TRNG_CTRL);

  up_udelay(10);
  ret = rv1103_trng_wait(RV1103_TRNG_ISTAT_RAND_RDY,
                         RV1103_TRNG_ISTAT_RAND_RDY);
  if (ret < 0)
    {
      putreg32(RV1103_TRNG_CTRL_NOP, RV1103_TRNG_CTRL);
      return ret;
    }

  for (i = 0; i < 8; i++)
    {
      value = getreg32(RV1103_TRNG_RAND0 + i * sizeof(uint32_t));
      output[i] = __builtin_bswap32(value);
      nonzero |= value != 0;
    }

  istat = getreg32(RV1103_TRNG_ISTAT);
  putreg32(istat, RV1103_TRNG_ISTAT);
  putreg32(RV1103_TRNG_CTRL_NOP, RV1103_TRNG_CTRL);

  if (!nonzero || (g_rv1103_trng.have_last &&
      memcmp(output, g_rv1103_trng.last, sizeof(g_rv1103_trng.last)) == 0))
    {
      return -EIO;
    }

  memcpy(g_rv1103_trng.last, output, sizeof(g_rv1103_trng.last));
  g_rv1103_trng.have_last = true;
  return 0;
}

static int rv1103_trng_hw_initialize(void)
{
  uint32_t discard[8];
  uint32_t istat;
  uint32_t version;
  int ret;

  if (g_rv1103_trng.initialized)
    {
      return 0;
    }

  /* A zero Rockchip gate bit enables the clock. */

  putreg32(RV1103_TRNG_CLOCK_BIT << 16, RV1103_PERI_CLKGATE_CON3);
  putreg32((RV1103_TRNG_RESET_BIT << 16) | RV1103_TRNG_RESET_BIT,
           RV1103_PERI_SOFTRST_CON3);
  up_udelay(10);
  putreg32(RV1103_TRNG_RESET_BIT << 16, RV1103_PERI_SOFTRST_CON3);

  version = getreg32(RV1103_TRNG_VERSION);
  if (version != RV1103_TRNG_VERSION_CODE)
    {
      syslog(LOG_ERR, "ERROR: TRNG version=%08lx expected=000046bc\n",
             (unsigned long)version);
      return -ENODEV;
    }

  ret = rv1103_trng_wait_seeded();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: TRNG seed timeout, stat=%08lx\n",
             (unsigned long)getreg32(RV1103_TRNG_STAT));
      return ret;
    }

  istat = getreg32(RV1103_TRNG_ISTAT);
  putreg32(istat, RV1103_TRNG_ISTAT);
  putreg32(1000, RV1103_TRNG_AUTO_RQSTS);

  ret = rv1103_trng_generate(discard);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: TRNG startup test failed: %d\n", ret);
      return ret;
    }

  g_rv1103_trng.initialized = true;
  syslog(LOG_INFO, "TRNG: hardware v1 startup test passed\n");
  return 0;
}

static ssize_t rv1103_trng_read(struct file *filep, char *buffer,
                                size_t buflen)
{
  uint32_t block[8];
  size_t copied = 0;
  size_t chunk;
  int ret;

  (void)filep;

  ret = nxmutex_lock(&g_rv1103_trng.lock);
  if (ret < 0)
    {
      return ret;
    }

  while (copied < buflen)
    {
      ret = rv1103_trng_generate(block);
      if (ret < 0)
        {
          nxmutex_unlock(&g_rv1103_trng.lock);
          return copied > 0 ? (ssize_t)copied : ret;
        }

      chunk = buflen - copied;
      if (chunk > sizeof(block))
        {
          chunk = sizeof(block);
        }

      memcpy(buffer + copied, block, chunk);
      copied += chunk;
    }

  nxmutex_unlock(&g_rv1103_trng.lock);
  return copied;
}

#ifdef CONFIG_DEV_RANDOM
void devrandom_register(void)
{
  if (rv1103_trng_hw_initialize() == 0)
    {
      register_driver("/dev/random", &g_rv1103_trng_fops, 0444, NULL);
    }
}
#endif

#ifdef CONFIG_DEV_URANDOM_ARCH
void devurandom_register(void)
{
  if (rv1103_trng_hw_initialize() == 0)
    {
      register_driver("/dev/urandom", &g_rv1103_trng_fops, 0444, NULL);
    }
}
#endif

#endif /* CONFIG_RV1103_TRNG */
