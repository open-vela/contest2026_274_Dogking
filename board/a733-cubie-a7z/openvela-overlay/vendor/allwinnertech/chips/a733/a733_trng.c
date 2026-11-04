/****************************************************************************
 * vendor/allwinnertech/chips/a733/a733_trng.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/* A733 (sun60iw2) Crypto Engine v5 true-random generator.
 *
 * The register sequence and descriptor layout are derived from Allwinner's
 * official sun60iw2 CE v5 driver.  This is deliberately a synchronous,
 * polling implementation: entropy is requested infrequently by TLS/SSH and
 * no CE interrupt ownership is required during this bring-up stage.
 */

#include <nuttx/config.h>

#ifdef CONFIG_A733_TRNG

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/fs/fs.h>
#include <nuttx/semaphore.h>

#include "arm64_internal.h"

#define A733_CCU_BASE              UINT64_C(0x02002000)
#define A733_CE_BASE               UINT64_C(0x04603000)
#define A733_CCU_MBUS_GATE         (A733_CCU_BASE + UINT64_C(0x05e4))
#define A733_CCU_CE_CLK            (A733_CCU_BASE + UINT64_C(0x0ac0))
#define A733_CCU_CE_BGR            (A733_CCU_BASE + UINT64_C(0x0ac4))

#define A733_CE_TSK0               UINT64_C(0x00)
#define A733_CE_TSK1               UINT64_C(0x04)
#define A733_CE_ICR                UINT64_C(0x08)
#define A733_CE_ISR                UINT64_C(0x0c)
#define A733_CE_TLR                UINT64_C(0x10)
#define A733_CE_ERR                UINT64_C(0x18)
#define A733_CE_VER                UINT64_C(0x90)
#define A733_CE_LPC                UINT64_C(0xd0)

#define A733_CE_CLK_GATE           (UINT32_C(1) << 31)
#define A733_CE_CLK_MUX_MASK       (UINT32_C(7) << 24)
#define A733_CE_CLK_DIV_MASK       UINT32_C(0x1f)
#define A733_CE_CLK_PLL400         (UINT32_C(1) << 24)
#define A733_CE_BUS_GATE           (UINT32_C(1) << 0)
#define A733_CE_SYS_GATE           (UINT32_C(1) << 1)
#define A733_CE_RESET              (UINT32_C(1) << 16)
#define A733_CE_SYS_RESET          (UINT32_C(1) << 17)
#define A733_CE_MBUS_KEY           UINT32_C(0x00040302)
#define A733_CE_MBUS_GATE          (UINT32_C(1) << 2)

#define A733_CE_PENDING0           UINT32_C(0x3)
#define A733_CE_HASH_RBG_START     (UINT32_C(1) << 10)
#define A733_CE_TRNG_DOUBLE        (UINT32_C(1) << 1)
#define A733_CE_TRNG_COMM          ((UINT32_C(1) << 16) | \
                                    (UINT32_C(1) << 12))
#define A733_CE_TRNG_COMMAND       ((UINT32_C(2) << 8) | UINT32_C(3))
#define A733_CE_TRNG_BLOCK         32u
#define A733_CE_POLL_LIMIT         100000u

struct a733_ce_scatter_s
{
  uint8_t src_addr[5];
  uint8_t dst_addr[5];
  uint8_t pad[2];
  uint32_t src_len;
  uint32_t dst_len;
};

struct a733_ce_rng_task_s
{
  uint32_t comm_ctl;
  uint32_t main_cmd;
  uint8_t data_len[5];
  uint8_t key_addr[5];
  uint8_t iv_addr[5];
  uint8_t pad;
  struct a733_ce_scatter_s scatter[8];
  uint8_t next_sg_addr[5];
  uint8_t next_task_addr[5];
  uint8_t pad2[2];
  uint32_t reserved[3];
  FAR struct a733_ce_rng_task_s *next_virt;
  uint64_t task_phys;
};

static sem_t g_a733_trng_lock;
static struct a733_ce_rng_task_s g_a733_trng_task
  __attribute__((aligned(64)));
static uint8_t g_a733_trng_output[A733_CE_TRNG_BLOCK]
  __attribute__((aligned(64)));
static uint8_t g_a733_trng_previous[A733_CE_TRNG_BLOCK];
static bool g_a733_trng_previous_valid;
static uint32_t g_a733_trng_version;

static void a733_ce_set_addr(FAR uint8_t address[5], uintptr_t physical)
{
  address[0] = physical & 0xff;
  address[1] = (physical >> 8) & 0xff;
  address[2] = (physical >> 16) & 0xff;
  address[3] = (physical >> 24) & 0xff;
  address[4] = (physical >> 32) & 0xff;
}

static int a733_ce_trng_block(FAR uint8_t output[A733_CE_TRNG_BLOCK])
{
  uintptr_t task_phys = (uintptr_t)&g_a733_trng_task;
  uintptr_t output_phys = (uintptr_t)g_a733_trng_output;
  uint32_t status;
  unsigned int poll;

  memset(&g_a733_trng_task, 0, sizeof(g_a733_trng_task));
  memset(g_a733_trng_output, 0, sizeof(g_a733_trng_output));
  g_a733_trng_task.comm_ctl = A733_CE_TRNG_COMM;
  g_a733_trng_task.main_cmd = A733_CE_TRNG_COMMAND;
  g_a733_trng_task.scatter[0].dst_len = A733_CE_TRNG_BLOCK;
  a733_ce_set_addr(g_a733_trng_task.scatter[0].dst_addr, output_phys);

  up_clean_dcache((uintptr_t)&g_a733_trng_task,
                  (uintptr_t)&g_a733_trng_task + sizeof(g_a733_trng_task));
  up_clean_dcache((uintptr_t)g_a733_trng_output,
                  (uintptr_t)g_a733_trng_output +
                  sizeof(g_a733_trng_output));
  UP_DSB();

  putreg32(A733_CE_PENDING0, A733_CE_BASE + A733_CE_ISR);
  putreg32(getreg32(A733_CE_BASE + A733_CE_ICR) | 1u,
           A733_CE_BASE + A733_CE_ICR);
  putreg32((uint32_t)task_phys, A733_CE_BASE + A733_CE_TSK0);
  putreg32((uint32_t)(task_phys >> 32), A733_CE_BASE + A733_CE_TSK1);
  putreg32(A733_CE_HASH_RBG_START, A733_CE_BASE + A733_CE_TLR);

  for (poll = 0; poll < A733_CE_POLL_LIMIT; poll++)
    {
      status = getreg32(A733_CE_BASE + A733_CE_ISR);
      if ((status & A733_CE_PENDING0) != 0)
        {
          break;
        }

      up_udelay(1);
    }

  if (poll == A733_CE_POLL_LIMIT)
    {
      return -ETIMEDOUT;
    }

  putreg32(A733_CE_PENDING0, A733_CE_BASE + A733_CE_ISR);
  if ((getreg32(A733_CE_BASE + A733_CE_ERR) & 0xffu) != 0)
    {
      return -EIO;
    }

  up_invalidate_dcache((uintptr_t)g_a733_trng_output,
                       (uintptr_t)g_a733_trng_output +
                       sizeof(g_a733_trng_output));
  UP_DSB();
  memcpy(output, g_a733_trng_output, A733_CE_TRNG_BLOCK);
  return OK;
}

static ssize_t a733_trng_read(FAR struct file *filep, FAR char *buffer,
                              size_t buflen)
{
  uint8_t block[A733_CE_TRNG_BLOCK];
  size_t copied = 0;
  int ret;

  (void)filep;
  ret = nxsem_wait_uninterruptible(&g_a733_trng_lock);
  if (ret < 0)
    {
      return ret;
    }

  while (copied < buflen)
    {
      size_t amount;

      ret = a733_ce_trng_block(block);
      if (ret < 0)
        {
          nxsem_post(&g_a733_trng_lock);
          return copied == 0 ? ret : (ssize_t)copied;
        }

      /* FIPS-style continuous health check: a consecutive identical raw
       * hardware block indicates a stuck generator.  Never return that
       * block to a cryptographic consumer.
       */

      if (g_a733_trng_previous_valid &&
          memcmp(block, g_a733_trng_previous, sizeof(block)) == 0)
        {
          memset(block, 0, sizeof(block));
          nxsem_post(&g_a733_trng_lock);
          return copied == 0 ? -EIO : (ssize_t)copied;
        }

      memcpy(g_a733_trng_previous, block, sizeof(block));
      g_a733_trng_previous_valid = true;

      amount = buflen - copied;
      if (amount > sizeof(block))
        {
          amount = sizeof(block);
        }

      memcpy(buffer + copied, block, amount);
      copied += amount;
    }

  memset(block, 0, sizeof(block));
  nxsem_post(&g_a733_trng_lock);
  return (ssize_t)copied;
}

static const struct file_operations g_a733_trng_fops =
{
  .read = a733_trng_read,
};

static bool a733_trng_sane(FAR const uint8_t block[A733_CE_TRNG_BLOCK])
{
  unsigned int i;

  for (i = 1; i < A733_CE_TRNG_BLOCK; i++)
    {
      if (block[i] != block[0])
        {
          return true;
        }
    }

  return false;
}

int a733_trng_initialize(void)
{
  uint8_t first[A733_CE_TRNG_BLOCK];
  uint8_t second[A733_CE_TRNG_BLOCK];
  uint32_t value;
  int ret;

  value = getreg32(A733_CCU_CE_BGR);
  value |= A733_CE_BUS_GATE | A733_CE_SYS_GATE |
           A733_CE_RESET | A733_CE_SYS_RESET;
  putreg32(value, A733_CCU_CE_BGR);

  value = getreg32(A733_CCU_MBUS_GATE);
  putreg32(value | A733_CE_MBUS_KEY | A733_CE_MBUS_GATE,
           A733_CCU_MBUS_GATE);

  value = getreg32(A733_CCU_CE_CLK);
  value &= ~(A733_CE_CLK_MUX_MASK | A733_CE_CLK_DIV_MASK);
  value |= A733_CE_CLK_GATE | A733_CE_CLK_PLL400;
  putreg32(value, A733_CCU_CE_CLK);
  up_udelay(20);

  g_a733_trng_version = getreg32(A733_CE_BASE + A733_CE_VER);
  if (g_a733_trng_version == 0 || g_a733_trng_version == UINT32_MAX)
    {
      return -ENODEV;
    }

  putreg32(getreg32(A733_CE_BASE + A733_CE_LPC) |
           A733_CE_TRNG_DOUBLE, A733_CE_BASE + A733_CE_LPC);
  nxsem_init(&g_a733_trng_lock, 0, 1);

  ret = a733_ce_trng_block(first);
  if (ret < 0)
    {
      return ret;
    }

  ret = a733_ce_trng_block(second);
  if (ret < 0)
    {
      return ret;
    }

  if (!a733_trng_sane(first) || !a733_trng_sane(second) ||
      memcmp(first, second, sizeof(first)) == 0)
    {
      return -EIO;
    }

  memcpy(g_a733_trng_previous, second, sizeof(second));
  g_a733_trng_previous_valid = true;

  ret = register_driver("/dev/random", &g_a733_trng_fops, 0444, NULL);
  if (ret < 0)
    {
      return ret;
    }

  ret = register_driver("/dev/urandom", &g_a733_trng_fops, 0444, NULL);
  if (ret < 0)
    {
      unregister_driver("/dev/random");
      return ret;
    }

  syslog(LOG_INFO,
         "A733 TRNG: CE v%08lx startup/continuous test passed; "
         "/dev/random and /dev/urandom ready\n",
         (unsigned long)g_a733_trng_version);
  return OK;
}

#endif /* CONFIG_A733_TRNG */
