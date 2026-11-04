/****************************************************************************
 * vendor/allwinnertech/chips/a733/a733_npu.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/* A733 VIP2 NPU bring-up checkpoint.
 *
 * This deliberately stops at the hardware boundary: it follows the official
 * sun60iw2 PCK600/CCU sequence, selects the documented 600 MHz / 800 mV OPP
 * (without changing the PMIC rail), releases reset, and reads the VIP identity
 * registers.  Later checkpoints add bounded IRQ delivery, guarded coherent
 * memory, direct command submission and the VIP2 page-descriptor MMU path.
 * Every active test remains user-triggered and recovers to the known-good
 * reset state so board boot is always non-blocking.
 */

#include <nuttx/config.h>

#ifdef CONFIG_A733_NPU_CHECKPOINT

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/fs/fs.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>

#include "arm64_internal.h"
#include "a733_npu_internal.h"
#include "a733_npu_memory.h"

#define A733_CCU_BASE              UINT64_C(0x02002000)
#define A733_PCK_BASE              UINT64_C(0x07060000)
#define A733_NPU_BASE              UINT64_C(0x03600000)

#define A733_PCK_NPU               (A733_PCK_BASE + UINT64_C(0x4000))
#define A733_PCK_POWER             UINT64_C(0x000)
#define A733_PCK_STATUS            UINT64_C(0x008)
#define A733_PCK_DEVICE_DELAY0     UINT64_C(0x170)
#define A733_PCK_DEVICE_DELAY1     UINT64_C(0x174)
#define A733_PCK_LOGIC_DELAY0      UINT64_C(0xc00)
#define A733_PCK_LOGIC_DELAY1      UINT64_C(0xc04)
#define A733_PCK_OFF2ON_DELAY      UINT64_C(0xc10)

#define A733_CCU_AHB_GATE          (A733_CCU_BASE + UINT64_C(0x05c0))
#define A733_CCU_MBUS_GATE         (A733_CCU_BASE + UINT64_C(0x05e0))
#define A733_CCU_NPU_CLK           (A733_CCU_BASE + UINT64_C(0x0b00))
#define A733_CCU_NPU_BGR           (A733_CCU_BASE + UINT64_C(0x0b04))

#define A733_AHB_KEY               UINT32_C(0x010000ff)
#define A733_MBUS_KEY              UINT32_C(0x41055800)
#define A733_NPU_AHB_GATE          (UINT32_C(1) << 6)
#define A733_NPU_MBUS_GATE         (UINT32_C(1) << 18)
#define A733_NPU_BUS_GATE          (UINT32_C(1) << 0)
#define A733_NPU_RESET_CORE        (UINT32_C(1) << 16)
#define A733_NPU_RESET_AXI         (UINT32_C(1) << 17)
#define A733_NPU_RESET_AHB         (UINT32_C(1) << 18)
#define A733_NPU_RESETS            (A733_NPU_RESET_CORE | \
                                    A733_NPU_RESET_AXI | \
                                    A733_NPU_RESET_AHB)
#define A733_NPU_CLK_GATE          (UINT32_C(1) << 31)
#define A733_NPU_CLK_MUX_MASK      (UINT32_C(7) << 24)
#define A733_NPU_CLK_DIV_MASK      UINT32_C(0x1f)
#define A733_NPU_CLK_PLL_PERI_600M (UINT32_C(2) << 24)

#define A733_VIP_CHIP_VERSION1     UINT64_C(0x020)
#define A733_VIP_CHIP_VERSION2     UINT64_C(0x024)
#define A733_VIP_CHIP_DATE         UINT64_C(0x028)
#define A733_VIP_CHIP_CID          UINT64_C(0x030)
#define A733_VIP_CONTROL           UINT64_C(0x000)
#define A733_VIP_IDLE              UINT64_C(0x004)
#define A733_VIP_IRQ_ACK           UINT64_C(0x010)
#define A733_VIP_IRQ_ENABLE        UINT64_C(0x014)
#define A733_VIP_CLOCK_GATE        UINT64_C(0x104)
#define A733_VIP_CLOCK_CONTROL     UINT64_C(0x10c)
#define A733_VIP_MODEL_CLOCK       UINT64_C(0x100)
#define A733_VIP_MEMORY_COUNTER    UINT64_C(0x03c)
#define A733_VIP_MMU_CONTROL       UINT64_C(0x388)
#define A733_VIP_MMU_STATUS        UINT64_C(0x384)
#define A733_VIP_MMU_EXCEPTION_ADDR UINT64_C(0x380)
#define A733_VIP_COMMAND_CONTROL   UINT64_C(0x3a4)
#define A733_VIP_SOFT_RESET        UINT64_C(0x3a8)
#define A733_VIP_MMU_PD_ENTRY      UINT64_C(0x3b4)
#define A733_VIP_COMMAND_ADDRESS   UINT64_C(0x654)
#define A733_VIP_BUS_CONTROL       UINT64_C(0x090)
#define A733_VIP_REORDER_CONTROL   UINT64_C(0x55c)
#define A733_VIP_OUTSTANDING       UINT64_C(0x414)

#define A733_NPU_IRQ               97
#define A733_VIP_IDLE_ALL          UINT32_C(0x7fffffff)
#define A733_VIP_EVENT_ID          4u
#define A733_VIP_EVENT_MASK        (UINT32_C(1) << A733_VIP_EVENT_ID)
#define A733_VIP_EVENT_WORD0       UINT32_C(0x08010e01)
#define A733_VIP_EVENT_WORD1       (UINT32_C(0x40) | A733_VIP_EVENT_ID)
#define A733_VIP_END_WORD0         UINT32_C(0x10000000)
#define A733_VIP_COMMAND_START     UINT32_C(0x0001ffff)
#define A733_VIP_SUBMIT_POLLS      1000u
#define A733_VIP_SUBMIT_DELAY_US   100u
#define A733_VIP_MMU_IRQ           UINT32_C(0x40000000)
#define A733_VIP_MMU_PD_CONTROL    UINT32_C(0x00000021)
#define A733_VIP_MMU_VIRTUAL       UINT32_C(0x01000000)
#define A733_VIP_MMU_MTLB_SHIFT    24u
#define A733_VIP_MMU_STLB_SHIFT    12u
#define A733_VIP_MMU_MTLB_ENTRIES  256u
#define A733_VIP_MMU_STLB_ENTRIES  4096u
#define A733_VIP_MMU_FREE_ENTRY    UINT32_C(0x00000002)
#define A733_VIP_MMU_PRESENT       UINT32_C(0x00000001)
#define A733_VIP_MMU_EXCEPTION     UINT32_C(0x00000002)
#define A733_VIP_MMU_WRITEABLE     UINT32_C(0x00000004)

struct a733_npu_state_s
{
  int checkpoint;
  uint32_t pck_power;
  uint32_t pck_status;
  uint32_t ahb_gate;
  uint32_t mbus_gate;
  uint32_t clock;
  uint32_t bgr;
  uint32_t version1;
  uint32_t version2;
  uint32_t date;
  uint32_t cid;
  uint32_t idle_before;
  uint32_t idle_after;
  uint32_t control_after;
  uint32_t mmu_after;
  uint32_t irq_initial;
  uint32_t irq_enable;
  volatile uint32_t irq_last;
  volatile uint32_t irq_count;
  int reset_checkpoint;
  int irq_checkpoint;
  int memory_checkpoint;
  int submit_checkpoint;
  int submit_recovery;
  uint32_t submit_physical;
  uint32_t submit_size;
  uint32_t submit_irq_before;
  uint32_t submit_irq_after;
  uint32_t submit_irq_value;
  uint32_t submit_idle;
  uint32_t submit_mmu;
  uint32_t submit_polls;
  int submit_guards;
  int mmutest_checkpoint;
  int mmutest_recovery;
  uint32_t mmutest_mtlb;
  uint32_t mmutest_stlb;
  uint32_t mmutest_command;
  uint32_t mmutest_virtual;
  uint32_t mmutest_pd_entry;
  uint32_t mmutest_control;
  uint32_t mmutest_irq_before;
  uint32_t mmutest_irq_after;
  uint32_t mmutest_irq_value;
  uint32_t mmutest_idle;
  uint32_t mmutest_polls;
  int mmutest_guards;
};

static struct a733_npu_state_s g_npu;
static struct a733_npu_buffer_s g_submit_buffer;
static bool g_submit_allocated;
static bool g_submit_busy;
static struct a733_npu_buffer_s g_mmu_mtlb;
static struct a733_npu_buffer_s g_mmu_stlb;
static struct a733_npu_buffer_s g_mmu_command;
static bool g_mmu_allocated;

static int a733_npu_interrupt(int irq, void *context, void *arg)
{
  uint32_t value;

  value = getreg32(A733_NPU_BASE + A733_VIP_IRQ_ACK);
  if (value != 0)
    {
      g_npu.irq_last = value;
      g_npu.irq_count++;
    }

  return OK;
}

static int a733_npu_soft_reset(void)
{
  uint32_t control;
  uint32_t idle;
  uint32_t mmu;
  unsigned int good = 0;
  unsigned int retry = 0;

  g_npu.idle_before = getreg32(A733_NPU_BASE + A733_VIP_IDLE);
  putreg32(UINT32_C(0x00070900), A733_NPU_BASE + A733_VIP_CONTROL);

  /* This is the bounded reset sequence used by the official VIP2 common
   * hardware layer.  Requiring two consecutive good observations catches a
   * transient de-isolation result without ever waiting indefinitely.
   */

  while (good < 2 && retry < 10)
    {
      putreg32(0, A733_NPU_BASE + A733_VIP_CLOCK_GATE);

      control = UINT32_C(0x01590880);
      control &= ~(UINT32_C(1) << 16);
      control |= UINT32_C(1) << 17;
      putreg32(control | 1u, A733_NPU_BASE + A733_VIP_CLOCK_CONTROL);
      putreg32(control, A733_NPU_BASE + A733_VIP_CLOCK_CONTROL);

      putreg32(UINT32_C(0x00070b00),
               A733_NPU_BASE + A733_VIP_CONTROL);
      up_udelay(5);
      putreg32(UINT32_C(0x00070900),
               A733_NPU_BASE + A733_VIP_CONTROL);
      up_udelay(20);

      control = UINT32_C(0x000f0900);
      putreg32(control, A733_NPU_BASE + A733_VIP_CONTROL);
      putreg32(1, A733_NPU_BASE + A733_VIP_SOFT_RESET);
      up_udelay(20);
      putreg32(0, A733_NPU_BASE + A733_VIP_SOFT_RESET);
      up_udelay(40);

      putreg32(control & ~(UINT32_C(1) << 12),
               A733_NPU_BASE + A733_VIP_CONTROL);
      control &= ~UINT32_C(0x80000);
      putreg32(control, A733_NPU_BASE + A733_VIP_CONTROL);
      up_udelay(50);

      idle = getreg32(A733_NPU_BASE + A733_VIP_IDLE);
      control = getreg32(A733_NPU_BASE + A733_VIP_CONTROL);
      mmu = getreg32(A733_NPU_BASE + A733_VIP_MMU_CONTROL);
      if ((idle & 1u) == 0 || (control & UINT32_C(0x30000)) !=
          UINT32_C(0x30000) || (mmu & 1u) != 0)
        {
          good = 0;
          retry++;
          continue;
        }

      good++;
    }

  putreg32(UINT32_C(0x00ffffff),
           A733_NPU_BASE + A733_VIP_REORDER_CONTROL);
  control = getreg32(A733_NPU_BASE + A733_VIP_BUS_CONTROL);
  putreg32(control | (UINT32_C(1) << 6),
           A733_NPU_BASE + A733_VIP_BUS_CONTROL);

  g_npu.idle_after = getreg32(A733_NPU_BASE + A733_VIP_IDLE);
  g_npu.control_after = getreg32(A733_NPU_BASE + A733_VIP_CONTROL);
  g_npu.mmu_after = getreg32(A733_NPU_BASE + A733_VIP_MMU_CONTROL);
  if (good < 2 || g_npu.idle_after != A733_VIP_IDLE_ALL)
    {
      return -EIO;
    }

  return OK;
}

static int a733_npu_irq_initialize(void)
{
  int ret;

  /* IRQ ACK is read-to-clear.  Drain any reset residue before exposing the
   * level-high GIC line, attach the handler, then use the official all-event
   * enable mask.  Real delivery is verified by the first submitted command.
   */

  g_npu.irq_initial = getreg32(A733_NPU_BASE + A733_VIP_IRQ_ACK);
  ret = irq_attach(A733_NPU_IRQ, a733_npu_interrupt, NULL);
  if (ret < 0)
    {
      return ret;
    }

  putreg32(UINT32_MAX, A733_NPU_BASE + A733_VIP_IRQ_ENABLE);
  g_npu.irq_enable = getreg32(A733_NPU_BASE + A733_VIP_IRQ_ENABLE);
  if (g_npu.irq_enable != UINT32_MAX)
    {
      irq_detach(A733_NPU_IRQ);
      return -EIO;
    }

  up_enable_irq(A733_NPU_IRQ);
  return OK;
}

/* Submit the smallest command stream used by the official VIP2 driver:
 * EVENT(block 6, id 4), followed by END.  The VIP MMU remains disabled, so
 * the command address is the arena's identity-mapped low-32 physical address.
 * This is deliberately user-triggered through /dev/a733-npu rather than run
 * during boot.  Every wait is bounded and failure always returns the block to
 * its already-proven reset state.
 */

static int a733_npu_submit_event(void)
{
  irqstate_t flags;
  uint32_t *command;
  uint32_t irq_before;
  uint32_t irq_after;
  uint32_t irq_value;
  uint32_t idle;
  unsigned int poll;
  int ret;

  flags = enter_critical_section();
  if (g_submit_busy)
    {
      leave_critical_section(flags);
      return -EBUSY;
    }

  g_submit_busy = true;
  leave_critical_section(flags);

  g_npu.submit_checkpoint = -EINPROGRESS;
  g_npu.submit_recovery = -EAGAIN;
  g_npu.submit_guards = -EAGAIN;
  g_npu.submit_polls = 0;
  g_npu.submit_irq_value = 0;

  if (!g_submit_allocated)
    {
      ret = a733_npu_mem_alloc(64, 64, &g_submit_buffer);
      if (ret < 0)
        {
          goto out;
        }

      g_submit_allocated = true;
    }

  g_npu.submit_physical = g_submit_buffer.physical;
  g_npu.submit_size = 16;
  g_npu.submit_mmu = getreg32(A733_NPU_BASE + A733_VIP_MMU_CONTROL);
  idle = getreg32(A733_NPU_BASE + A733_VIP_IDLE);
  if ((g_npu.submit_mmu & 1u) != 0 || idle != A733_VIP_IDLE_ALL)
    {
      ret = -EBUSY;
      goto recover;
    }

  command = (uint32_t *)g_submit_buffer.cpu;
  memset(command, 0, g_submit_buffer.size);
  command[0] = A733_VIP_EVENT_WORD0;
  command[1] = A733_VIP_EVENT_WORD1;
  command[2] = A733_VIP_END_WORD0;
  command[3] = 0;
  a733_npu_mem_clean(&g_submit_buffer);
  UP_DSB();

  /* Drain reset residue before taking the sequence number.  Reading ACK is
   * the hardware-defined clear operation; normal completion is read by ISR.
   */

  (void)getreg32(A733_NPU_BASE + A733_VIP_IRQ_ACK);
  g_npu.irq_last = 0;
  irq_before = g_npu.irq_count;
  g_npu.submit_irq_before = irq_before;
  putreg32(UINT32_MAX, A733_NPU_BASE + A733_VIP_IRQ_ENABLE);
  putreg32(g_submit_buffer.physical,
           A733_NPU_BASE + A733_VIP_COMMAND_ADDRESS);
  UP_DSB();
  putreg32(A733_VIP_COMMAND_START,
           A733_NPU_BASE + A733_VIP_COMMAND_CONTROL);

  for (poll = 0; poll < A733_VIP_SUBMIT_POLLS; poll++)
    {
      if (g_npu.irq_count != irq_before)
        {
          break;
        }

      up_udelay(A733_VIP_SUBMIT_DELAY_US);
    }

  g_npu.submit_polls = poll;
  irq_after = g_npu.irq_count;
  irq_value = g_npu.irq_last;
  g_npu.submit_irq_after = irq_after;
  g_npu.submit_irq_value = irq_value;
  if (irq_after == irq_before || (irq_value & A733_VIP_EVENT_MASK) == 0)
    {
      ret = -ETIMEDOUT;
      goto recover;
    }

  /* EVENT IRQ can arrive just before END retires.  Give the front end a
   * separate bounded 10 ms window to return to the all-module idle state.
   */

  for (poll = 0; poll < 100; poll++)
    {
      idle = getreg32(A733_NPU_BASE + A733_VIP_IDLE);
      if (idle == A733_VIP_IDLE_ALL)
        {
          break;
        }

      up_udelay(100);
    }

  g_npu.submit_idle = idle;
  g_npu.submit_guards = a733_npu_mem_check_guards();
  if (idle != A733_VIP_IDLE_ALL || g_npu.submit_guards < 0)
    {
      ret = -EIO;
      goto recover;
    }

  g_npu.submit_recovery = OK;
  ret = OK;
  goto out;

recover:
  /* Disable sources while restoring the known-good block state. */

  putreg32(0, A733_NPU_BASE + A733_VIP_IRQ_ENABLE);
  g_npu.submit_irq_value |= getreg32(A733_NPU_BASE + A733_VIP_IRQ_ACK);
  g_npu.submit_idle = getreg32(A733_NPU_BASE + A733_VIP_IDLE);
  g_npu.submit_guards = a733_npu_mem_check_guards();
  g_npu.submit_recovery = a733_npu_soft_reset();
  putreg32(UINT32_MAX, A733_NPU_BASE + A733_VIP_IRQ_ENABLE);

out:
  g_npu.submit_checkpoint = ret;
  flags = enter_critical_section();
  g_submit_busy = false;
  leave_critical_section(flags);
  return ret;
}

/* Prove the official VIP2 page-descriptor MMU path with a translated command
 * fetch.  The MTLB uses the vendor's 1 KiB/32-bit layout (256 entries, one
 * entry per 16 MiB VA range); a 16 KiB 4K-page STLB maps exactly one guarded
 * low32 command page at VA 0x01000000.  The command's physical address is
 * intentionally different from its VIP virtual address, so a completion IRQ
 * is evidence that the page walk and command fetch both worked.
 */

static int a733_npu_mmu_test(void)
{
  irqstate_t flags;
  uint32_t *mtlb;
  uint32_t *stlb;
  uint32_t *command;
  uint32_t mtlb_index;
  uint32_t stlb_index;
  uint32_t irq_before;
  uint32_t irq_after;
  uint32_t irq_value;
  uint32_t idle;
  unsigned int poll;
  int ret;

  flags = enter_critical_section();
  if (g_submit_busy)
    {
      leave_critical_section(flags);
      return -EBUSY;
    }

  g_submit_busy = true;
  leave_critical_section(flags);

  g_npu.mmutest_checkpoint = -EINPROGRESS;
  g_npu.mmutest_recovery = -EAGAIN;
  g_npu.mmutest_guards = -EAGAIN;
  g_npu.mmutest_polls = 0;
  g_npu.mmutest_irq_value = 0;

  if (!g_mmu_allocated)
    {
      ret = a733_npu_mem_alloc(A733_VIP_MMU_MTLB_ENTRIES *
                               sizeof(uint32_t), 4096, &g_mmu_mtlb);
      if (ret < 0)
        {
          goto out;
        }

      ret = a733_npu_mem_alloc(A733_VIP_MMU_STLB_ENTRIES *
                               sizeof(uint32_t), 16384, &g_mmu_stlb);
      if (ret < 0)
        {
          goto out;
        }

      ret = a733_npu_mem_alloc(4096, 4096, &g_mmu_command);
      if (ret < 0)
        {
          goto out;
        }

      g_mmu_allocated = true;
    }

  g_npu.mmutest_mtlb = g_mmu_mtlb.physical;
  g_npu.mmutest_stlb = g_mmu_stlb.physical;
  g_npu.mmutest_command = g_mmu_command.physical;
  g_npu.mmutest_virtual = A733_VIP_MMU_VIRTUAL;

  if ((g_mmu_mtlb.physical & 4095u) != 0 ||
      (g_mmu_stlb.physical & 16383u) != 0 ||
      (g_mmu_command.physical & 4095u) != 0)
    {
      ret = -EFAULT;
      goto recover;
    }

  mtlb = (uint32_t *)g_mmu_mtlb.cpu;
  stlb = (uint32_t *)g_mmu_stlb.cpu;
  command = (uint32_t *)g_mmu_command.cpu;
  memset(mtlb, 0, g_mmu_mtlb.size);
  for (poll = 0; poll < A733_VIP_MMU_STLB_ENTRIES; poll++)
    {
      stlb[poll] = A733_VIP_MMU_FREE_ENTRY;
    }

  memset(command, 0, g_mmu_command.size);
  command[0] = A733_VIP_EVENT_WORD0;
  command[1] = A733_VIP_EVENT_WORD1;
  command[2] = A733_VIP_END_WORD0;
  command[3] = 0;

  mtlb_index = A733_VIP_MMU_VIRTUAL >> A733_VIP_MMU_MTLB_SHIFT;
  stlb_index = (A733_VIP_MMU_VIRTUAL >> A733_VIP_MMU_STLB_SHIFT) &
               (A733_VIP_MMU_STLB_ENTRIES - 1u);
  mtlb[mtlb_index] = g_mmu_stlb.physical | A733_VIP_MMU_PRESENT;
  stlb[stlb_index] = (g_mmu_command.physical & UINT32_C(0xfffff000)) |
                     A733_VIP_MMU_PRESENT | A733_VIP_MMU_EXCEPTION |
                     A733_VIP_MMU_WRITEABLE;

  a733_npu_mem_clean(&g_mmu_mtlb);
  a733_npu_mem_clean(&g_mmu_stlb);
  a733_npu_mem_clean(&g_mmu_command);
  UP_DSB();

  idle = getreg32(A733_NPU_BASE + A733_VIP_IDLE);
  if ((getreg32(A733_NPU_BASE + A733_VIP_MMU_CONTROL) & 1u) != 0 ||
      idle != A733_VIP_IDLE_ALL)
    {
      ret = a733_npu_soft_reset();
      if (ret < 0)
        {
          goto recover;
        }
    }

  (void)getreg32(A733_NPU_BASE + A733_VIP_IRQ_ACK);
  g_npu.irq_last = 0;
  irq_before = g_npu.irq_count;
  g_npu.mmutest_irq_before = irq_before;

  /* Official pd-mode encoding: ((MTLB physical >> 12) << 4) | 1. */

  g_npu.mmutest_pd_entry = (g_mmu_mtlb.physical >> 8) | 1u;
  putreg32(g_npu.mmutest_pd_entry,
           A733_NPU_BASE + A733_VIP_MMU_PD_ENTRY);
  putreg32(A733_VIP_MMU_PD_CONTROL,
           A733_NPU_BASE + A733_VIP_MMU_CONTROL);
  UP_DSB();
  g_npu.mmutest_control =
    getreg32(A733_NPU_BASE + A733_VIP_MMU_CONTROL);
  if ((g_npu.mmutest_control & A733_VIP_MMU_PD_CONTROL) !=
      A733_VIP_MMU_PD_CONTROL)
    {
      ret = -EIO;
      goto recover;
    }

  putreg32(UINT32_MAX, A733_NPU_BASE + A733_VIP_IRQ_ENABLE);
  putreg32(A733_VIP_MMU_VIRTUAL,
           A733_NPU_BASE + A733_VIP_COMMAND_ADDRESS);
  UP_DSB();
  putreg32(A733_VIP_COMMAND_START,
           A733_NPU_BASE + A733_VIP_COMMAND_CONTROL);

  for (poll = 0; poll < A733_VIP_SUBMIT_POLLS; poll++)
    {
      if (g_npu.irq_count != irq_before)
        {
          break;
        }

      up_udelay(A733_VIP_SUBMIT_DELAY_US);
    }

  g_npu.mmutest_polls = poll;
  irq_after = g_npu.irq_count;
  irq_value = g_npu.irq_last;
  g_npu.mmutest_irq_after = irq_after;
  g_npu.mmutest_irq_value = irq_value;
  if (irq_after == irq_before ||
      (irq_value & A733_VIP_EVENT_MASK) == 0 ||
      (irq_value & A733_VIP_MMU_IRQ) != 0)
    {
      ret = (irq_value & A733_VIP_MMU_IRQ) != 0 ? -EFAULT : -ETIMEDOUT;
      goto recover;
    }

  for (poll = 0; poll < 100; poll++)
    {
      idle = getreg32(A733_NPU_BASE + A733_VIP_IDLE);
      if (idle == A733_VIP_IDLE_ALL)
        {
          break;
        }

      up_udelay(100);
    }

  g_npu.mmutest_idle = idle;
  g_npu.mmutest_guards = a733_npu_mem_check_guards();
  if (idle != A733_VIP_IDLE_ALL || g_npu.mmutest_guards < 0)
    {
      ret = -EIO;
      goto recover;
    }

  ret = OK;

recover:
  /* Always return to the v37 MMU-disabled state.  This makes direct submit
   * reusable after mmutest and provides a bounded recovery check on success
   * as well as failure.
   */

  putreg32(0, A733_NPU_BASE + A733_VIP_IRQ_ENABLE);
  g_npu.mmutest_irq_value |=
    getreg32(A733_NPU_BASE + A733_VIP_IRQ_ACK);
  g_npu.mmutest_idle = getreg32(A733_NPU_BASE + A733_VIP_IDLE);
  g_npu.mmutest_guards = a733_npu_mem_check_guards();
  g_npu.mmutest_recovery = a733_npu_soft_reset();
  putreg32(UINT32_MAX, A733_NPU_BASE + A733_VIP_IRQ_ENABLE);
  if (ret == OK && g_npu.mmutest_recovery < 0)
    {
      ret = g_npu.mmutest_recovery;
    }

out:
  g_npu.mmutest_checkpoint = ret;
  flags = enter_critical_section();
  g_submit_busy = false;
  leave_critical_section(flags);
  return ret;
}

int a733_npu_hw_execute(uint32_t pd_entry, uint32_t command_address,
                        uint32_t timeout_ms, volatile bool *cancel,
                        bool restore,
                        struct a733_npu_hw_result_s *result)
{
  irqstate_t flags;
  uint32_t irq_before;
  uint32_t irq_value;
  uint32_t idle;
  uint32_t limit;
  uint32_t poll;
  int ret;

  if (result == NULL || (pd_entry & 1u) == 0 ||
      (command_address & 7u) != 0 || timeout_ms == 0 ||
      timeout_ms > 5000u)
    {
      return -EINVAL;
    }

  memset(result, 0, sizeof(*result));
  result->recovery = -EAGAIN;
  flags = enter_critical_section();
  if (g_submit_busy)
    {
      leave_critical_section(flags);
      return -EBUSY;
    }

  g_submit_busy = true;
  leave_critical_section(flags);

  if ((getreg32(A733_NPU_BASE + A733_VIP_MMU_CONTROL) & 1u) != 0 ||
      getreg32(A733_NPU_BASE + A733_VIP_IDLE) != A733_VIP_IDLE_ALL)
    {
      ret = a733_npu_soft_reset();
      if (ret < 0)
        {
          goto out;
        }
    }

  (void)getreg32(A733_NPU_BASE + A733_VIP_IRQ_ACK);
  g_npu.irq_last = 0;
  irq_before = g_npu.irq_count;
  result->irq_before = irq_before;
  putreg32(pd_entry, A733_NPU_BASE + A733_VIP_MMU_PD_ENTRY);
  putreg32(A733_VIP_MMU_PD_CONTROL,
           A733_NPU_BASE + A733_VIP_MMU_CONTROL);
  UP_DSB();
  if ((getreg32(A733_NPU_BASE + A733_VIP_MMU_CONTROL) &
       A733_VIP_MMU_PD_CONTROL) != A733_VIP_MMU_PD_CONTROL)
    {
      ret = -EIO;
      goto recover;
    }

  putreg32(UINT32_MAX, A733_NPU_BASE + A733_VIP_IRQ_ENABLE);
  putreg32(command_address, A733_NPU_BASE + A733_VIP_COMMAND_ADDRESS);
  UP_DSB();
  putreg32(A733_VIP_COMMAND_START,
           A733_NPU_BASE + A733_VIP_COMMAND_CONTROL);

  limit = timeout_ms * 10u;
  for (poll = 0; poll < limit; poll++)
    {
      if (cancel != NULL && *cancel)
        {
          ret = -ECANCELED;
          goto recover;
        }

      if (g_npu.irq_count != irq_before)
        {
          break;
        }

      up_udelay(100);
    }

  result->polls = poll;
  result->irq_after = g_npu.irq_count;
  result->irq_value = g_npu.irq_last;
  irq_value = result->irq_value;
  if (result->irq_after == irq_before)
    {
      ret = -ETIMEDOUT;
      goto recover;
    }

  if ((irq_value & A733_VIP_MMU_IRQ) != 0)
    {
      ret = -EFAULT;
      goto recover;
    }

  if ((irq_value & A733_VIP_EVENT_MASK) == 0)
    {
      ret = -EIO;
      goto recover;
    }

  for (poll = 0; poll < 100; poll++)
    {
      idle = getreg32(A733_NPU_BASE + A733_VIP_IDLE);
      if (idle == A733_VIP_IDLE_ALL)
        {
          break;
        }

      up_udelay(100);
    }

  result->idle = idle;
  ret = idle == A733_VIP_IDLE_ALL ? OK : -EIO;
  if (ret == OK && !restore)
    {
      goto out;
    }

recover:
  /* Vivante MMUv2 secure-mode fault diagnostics.  Capture these before the
   * soft reset clears them; 0x384 reports the exception class and 0x380 the
   * faulting virtual address.  Keeping this in every result also makes a
   * golden-output-complete late fault distinguishable from a missing map.
   */

  result->mmu_status = getreg32(A733_NPU_BASE + A733_VIP_MMU_STATUS);
  result->mmu_exception =
    getreg32(A733_NPU_BASE + A733_VIP_MMU_EXCEPTION_ADDR);
  putreg32(0, A733_NPU_BASE + A733_VIP_IRQ_ENABLE);
  result->irq_value |= getreg32(A733_NPU_BASE + A733_VIP_IRQ_ACK);
  result->idle = getreg32(A733_NPU_BASE + A733_VIP_IDLE);
  result->recovery = a733_npu_soft_reset();
  putreg32(UINT32_MAX, A733_NPU_BASE + A733_VIP_IRQ_ENABLE);
  if (ret == OK && result->recovery < 0)
    {
      ret = result->recovery;
    }

out:
  flags = enter_critical_section();
  g_submit_busy = false;
  leave_critical_section(flags);
  return ret;
}

/* Execute a captured VIPLite network from the same initialized state as the
 * official Linux VIP2 driver.  The device init command is deliberately kept
 * in the same MMU transaction as the network command: a software reset in
 * between would discard the NN-core and VIP-SRAM programming it establishes.
 */

int a733_npu_hw_execute_initialized(uint32_t pd_entry,
                                    uint32_t init_command_address,
                                    uint32_t command_address,
                                    uint32_t timeout_ms,
                                    volatile bool *cancel,
                                    struct a733_npu_hw_result_s *result)
{
  irqstate_t flags;
  uint32_t control;
  uint32_t irq_before;
  uint32_t irq_value;
  uint32_t idle = 0;
  uint32_t limit;
  uint32_t poll;
  int ret;

  if (result == NULL || (pd_entry & 1u) == 0 ||
      (init_command_address & 7u) != 0 ||
      (command_address & 7u) != 0 || timeout_ms == 0 ||
      timeout_ms > 5000u)
    {
      return -EINVAL;
    }

  memset(result, 0, sizeof(*result));
  result->recovery = -EAGAIN;
  flags = enter_critical_section();
  if (g_submit_busy)
    {
      leave_critical_section(flags);
      return -EBUSY;
    }

  g_submit_busy = true;
  leave_critical_section(flags);

  ret = a733_npu_soft_reset();
  if (ret < 0)
    {
      goto out;
    }

  /* init_vip() from the vendor VIP2 2.0.3.2 driver, specialized with the
   * A733 feature database: SH conformance is present, AXI SRAM is absent,
   * and therefore bus reorder is bypassed.
   */

  putreg32(UINT32_C(0x00070900), A733_NPU_BASE + A733_VIP_CONTROL);
  putreg32(UINT32_C(0x00000002), A733_NPU_BASE + A733_VIP_SOFT_RESET);
  putreg32(UINT32_MAX, A733_NPU_BASE + A733_VIP_MEMORY_COUNTER);
  putreg32(0, A733_NPU_BASE + A733_VIP_MEMORY_COUNTER);
  putreg32(UINT32_C(0x00140021),
           A733_NPU_BASE + A733_VIP_MODEL_CLOCK);

  putreg32(pd_entry, A733_NPU_BASE + A733_VIP_MMU_PD_ENTRY);
  putreg32(A733_VIP_MMU_PD_CONTROL,
           A733_NPU_BASE + A733_VIP_MMU_CONTROL);
  UP_DSB();
  if ((getreg32(A733_NPU_BASE + A733_VIP_MMU_CONTROL) &
       A733_VIP_MMU_PD_CONTROL) != A733_VIP_MMU_PD_CONTROL)
    {
      ret = -EIO;
      goto recover;
    }

  putreg32(0, A733_NPU_BASE + A733_VIP_CLOCK_GATE);
  control = getreg32(A733_NPU_BASE + A733_VIP_CLOCK_CONTROL);
  control &= ~(UINT32_C(1) << 16);
  control |= UINT32_C(1) << 17;
  putreg32(control, A733_NPU_BASE + A733_VIP_CLOCK_CONTROL);
  control = getreg32(A733_NPU_BASE + A733_VIP_BUS_CONTROL);
  putreg32(control | (UINT32_C(1) << 6),
           A733_NPU_BASE + A733_VIP_BUS_CONTROL);
  control = getreg32(A733_NPU_BASE + A733_VIP_OUTSTANDING);
  putreg32(control & ~UINT32_C(1),
           A733_NPU_BASE + A733_VIP_OUTSTANDING);

  /* The init stream has END but no EVENT.  Match the vendor driver's idle
   * polling path, including its 10 us post-trigger settling delay.
   */

  (void)getreg32(A733_NPU_BASE + A733_VIP_IRQ_ACK);
  putreg32(UINT32_MAX, A733_NPU_BASE + A733_VIP_IRQ_ENABLE);
  putreg32(init_command_address,
           A733_NPU_BASE + A733_VIP_COMMAND_ADDRESS);
  UP_DSB();
  putreg32(A733_VIP_COMMAND_START,
           A733_NPU_BASE + A733_VIP_COMMAND_CONTROL);
  UP_DSB();
  up_udelay(10);
  for (poll = 0; poll < 100; poll++)
    {
      idle = getreg32(A733_NPU_BASE + A733_VIP_IDLE);
      if (idle == A733_VIP_IDLE_ALL)
        {
          break;
        }

      up_udelay(100);
    }

  if (idle != A733_VIP_IDLE_ALL)
    {
      ret = -ETIMEDOUT;
      goto recover;
    }

  up_udelay(10);
  (void)getreg32(A733_NPU_BASE + A733_VIP_IRQ_ACK);
  g_npu.irq_last = 0;
  irq_before = g_npu.irq_count;
  result->irq_before = irq_before;

  putreg32(command_address, A733_NPU_BASE + A733_VIP_COMMAND_ADDRESS);
  UP_DSB();
  putreg32(A733_VIP_COMMAND_START,
           A733_NPU_BASE + A733_VIP_COMMAND_CONTROL);

  limit = timeout_ms * 10u;
  for (poll = 0; poll < limit; poll++)
    {
      if (cancel != NULL && *cancel)
        {
          ret = -ECANCELED;
          goto recover;
        }

      if (g_npu.irq_count != irq_before)
        {
          break;
        }

      up_udelay(100);
    }

  result->polls = poll;
  result->irq_after = g_npu.irq_count;
  result->irq_value = g_npu.irq_last;
  irq_value = result->irq_value;
  if (result->irq_after == irq_before)
    {
      ret = -ETIMEDOUT;
      goto recover;
    }

  if ((irq_value & A733_VIP_MMU_IRQ) != 0)
    {
      ret = -EFAULT;
      goto recover;
    }

  if ((irq_value & A733_VIP_EVENT_MASK) == 0)
    {
      ret = -EIO;
      goto recover;
    }

  for (poll = 0; poll < 100; poll++)
    {
      idle = getreg32(A733_NPU_BASE + A733_VIP_IDLE);
      if (idle == A733_VIP_IDLE_ALL)
        {
          break;
        }

      up_udelay(100);
    }

  result->idle = idle;
  ret = idle == A733_VIP_IDLE_ALL ? OK : -EIO;

recover:
  /* Snapshot the secure MMUv2 diagnostics before recovery clears them. */

  result->mmu_status = getreg32(A733_NPU_BASE + A733_VIP_MMU_STATUS);
  result->mmu_exception =
    getreg32(A733_NPU_BASE + A733_VIP_MMU_EXCEPTION_ADDR);
  putreg32(0, A733_NPU_BASE + A733_VIP_IRQ_ENABLE);
  result->irq_value |= getreg32(A733_NPU_BASE + A733_VIP_IRQ_ACK);
  result->idle = getreg32(A733_NPU_BASE + A733_VIP_IDLE);
  result->recovery = a733_npu_soft_reset();
  putreg32(UINT32_MAX, A733_NPU_BASE + A733_VIP_IRQ_ENABLE);
  if (ret == OK && result->recovery < 0)
    {
      ret = result->recovery;
    }

out:
  flags = enter_critical_section();
  g_submit_busy = false;
  leave_critical_section(flags);
  return ret;
}

int a733_npu_hw_reset(void)
{
  return a733_npu_soft_reset();
}

uint32_t a733_npu_hw_cid(void)
{
  return g_npu.cid;
}

static void a733_npu_snapshot(void)
{
  g_npu.pck_power = getreg32(A733_PCK_NPU + A733_PCK_POWER);
  g_npu.pck_status = getreg32(A733_PCK_NPU + A733_PCK_STATUS);
  g_npu.ahb_gate = getreg32(A733_CCU_AHB_GATE);
  g_npu.mbus_gate = getreg32(A733_CCU_MBUS_GATE);
  g_npu.clock = getreg32(A733_CCU_NPU_CLK);
  g_npu.bgr = getreg32(A733_CCU_NPU_BGR);
}

static int a733_npu_power_on(void)
{
  uint32_t value;
  unsigned int retry;

  /* Values and offsets are taken verbatim from sun60iw2_pck600_pmu. */

  putreg32(UINT32_C(0x001f1f1f),
           A733_PCK_NPU + A733_PCK_DEVICE_DELAY0);
  putreg32(UINT32_C(0x00001f1f),
           A733_PCK_NPU + A733_PCK_DEVICE_DELAY1);
  putreg32(UINT32_C(0x08080808),
           A733_PCK_NPU + A733_PCK_LOGIC_DELAY0);
  putreg32(UINT32_C(0x00000808),
           A733_PCK_NPU + A733_PCK_LOGIC_DELAY1);
  putreg32(UINT32_C(0x00000008),
           A733_PCK_NPU + A733_PCK_OFF2ON_DELAY);

  value = getreg32(A733_PCK_NPU + A733_PCK_STATUS);
  if ((value & UINT32_C(0xf)) != UINT32_C(0x8))
    {
      value = getreg32(A733_PCK_NPU + A733_PCK_POWER);
      value = (value & ~UINT32_C(0xf)) | UINT32_C(0x8);
      putreg32(value, A733_PCK_NPU + A733_PCK_POWER);

      for (retry = 0; retry < 10000; retry++)
        {
          if ((getreg32(A733_PCK_NPU + A733_PCK_STATUS) &
               UINT32_C(0xf)) == UINT32_C(0x8))
            {
              break;
            }

          up_udelay(1);
        }

      if (retry == 10000)
        {
          return -ETIMEDOUT;
        }
    }

  /* Match the official VIP2 ordering: release AHB/AXI/core reset, enable the
   * protected fabric gates, select pll-peri0-600m, then enable bus/module.
   */

  value = getreg32(A733_CCU_NPU_BGR);
  putreg32(value | A733_NPU_RESETS, A733_CCU_NPU_BGR);

  value = getreg32(A733_CCU_AHB_GATE);
  putreg32(value | A733_AHB_KEY | A733_NPU_AHB_GATE,
           A733_CCU_AHB_GATE);

  value = getreg32(A733_CCU_MBUS_GATE);
  putreg32(value | A733_MBUS_KEY | A733_NPU_MBUS_GATE,
           A733_CCU_MBUS_GATE);

  value = getreg32(A733_CCU_NPU_BGR);
  putreg32(value | A733_NPU_BUS_GATE | A733_NPU_RESETS,
           A733_CCU_NPU_BGR);

  value = getreg32(A733_CCU_NPU_CLK);
  value &= ~(A733_NPU_CLK_MUX_MASK | A733_NPU_CLK_DIV_MASK);
  value |= A733_NPU_CLK_PLL_PERI_600M | A733_NPU_CLK_GATE;
  putreg32(value, A733_CCU_NPU_CLK);
  up_udelay(20);

  a733_npu_snapshot();
  if ((g_npu.pck_status & UINT32_C(0xf)) != UINT32_C(0x8) ||
      (g_npu.clock & A733_NPU_CLK_GATE) == 0 ||
      (g_npu.bgr & (A733_NPU_BUS_GATE | A733_NPU_RESETS)) !=
      (A733_NPU_BUS_GATE | A733_NPU_RESETS))
    {
      return -EIO;
    }

  return OK;
}

static int a733_npu_probe(void)
{
  int ret;

  ret = a733_npu_power_on();
  if (ret < 0)
    {
      return ret;
    }

  g_npu.version1 = getreg32(A733_NPU_BASE + A733_VIP_CHIP_VERSION1);
  g_npu.version2 = getreg32(A733_NPU_BASE + A733_VIP_CHIP_VERSION2);
  g_npu.date = getreg32(A733_NPU_BASE + A733_VIP_CHIP_DATE);
  g_npu.cid = getreg32(A733_NPU_BASE + A733_VIP_CHIP_CID);

  if ((g_npu.version1 == 0 && g_npu.version2 == 0 &&
       g_npu.date == 0 && g_npu.cid == 0) ||
      (g_npu.version1 == UINT32_MAX && g_npu.version2 == UINT32_MAX &&
       g_npu.date == UINT32_MAX && g_npu.cid == UINT32_MAX))
    {
      return -ENODEV;
    }

  g_npu.reset_checkpoint = a733_npu_soft_reset();
  if (g_npu.reset_checkpoint < 0)
    {
      return g_npu.reset_checkpoint;
    }

  /* Reset must not make the identity aperture disappear. */

  g_npu.version1 = getreg32(A733_NPU_BASE + A733_VIP_CHIP_VERSION1);
  g_npu.version2 = getreg32(A733_NPU_BASE + A733_VIP_CHIP_VERSION2);
  g_npu.date = getreg32(A733_NPU_BASE + A733_VIP_CHIP_DATE);
  g_npu.cid = getreg32(A733_NPU_BASE + A733_VIP_CHIP_CID);

  g_npu.irq_checkpoint = a733_npu_irq_initialize();
  if (g_npu.irq_checkpoint < 0)
    {
      return g_npu.irq_checkpoint;
    }

  g_npu.memory_checkpoint = a733_npu_mem_initialize();
  if (g_npu.memory_checkpoint < 0)
    {
      return g_npu.memory_checkpoint;
    }

  return OK;
}

static ssize_t a733_npu_read(struct file *filep, char *buffer,
                             size_t buflen)
{
  char report[2048];
  struct a733_npu_mem_stats_s memory;
  size_t length;
  size_t copy;

  a733_npu_snapshot();
  a733_npu_mem_get_stats(&memory);
  length = (size_t)snprintf(report, sizeof(report),
    "A733 VIP2 NPU checkpoint\n"
    "source: official AW_NNA_VIP + NNA_VIP2 (not galcore/VIP1)\n"
    "resource: base=03600000 size=1000 ccu=02002000 "
    "gic-spi=65 nuttx-irq=97\n"
    "power: pck-domain=4 command=%08lx status=%08lx state=%s\n"
    "clock: target=600000000Hz parent=pll-peri0-600m ahb=%08lx "
    "mbus=%08lx npu=%08lx bgr=%08lx\n"
    "identity: version1=%08lx version2=%08lx date=%08lx cid-pid=%08lx\n"
    "reset: checkpoint=%d idle=%08lx->%08lx control=%08lx mmu=%08lx\n"
    "irq: line=%d trigger=level-high checkpoint=%d initial=%08lx "
    "enable=%08lx count=%lu last=%08lx\n"
    "memory: checkpoint=%d base=%016llx end=%016llx size=%lu "
    "used=%lu alloc=%u low32=%d guards=%d selftest=%d\n"
    "submit: checkpoint=%d physical=%08lx size=%lu mmu=%08lx "
    "irq=%lu->%lu value=%08lx polls=%lu idle=%08lx guards=%d "
    "recovery=%d\n"
    "mmutest: checkpoint=%d mtlb=%08lx stlb=%08lx physical=%08lx "
    "virtual=%08lx pd=%08lx control=%08lx irq=%lu->%lu value=%08lx "
    "polls=%lu idle=%08lx guards=%d recovery=%d\n"
    "checkpoint=%d\n"
    "scope: power/clock/reset/identity, IRQ attachment and low32 coherent "
    "arena complete; controls='echo submit|mmutest > /dev/a733-npu'; "
    "VIP2 PD-MMU translated fetch test available; full userspace runtime "
    "pending\n",
    (unsigned long)g_npu.pck_power,
    (unsigned long)g_npu.pck_status,
    (g_npu.pck_status & 0xfu) == 8u ? "on" : "off",
    (unsigned long)g_npu.ahb_gate,
    (unsigned long)g_npu.mbus_gate,
    (unsigned long)g_npu.clock,
    (unsigned long)g_npu.bgr,
    (unsigned long)g_npu.version1,
    (unsigned long)g_npu.version2,
    (unsigned long)g_npu.date,
    (unsigned long)g_npu.cid,
    g_npu.reset_checkpoint,
    (unsigned long)g_npu.idle_before,
    (unsigned long)g_npu.idle_after,
    (unsigned long)g_npu.control_after,
    (unsigned long)g_npu.mmu_after,
    A733_NPU_IRQ,
    g_npu.irq_checkpoint,
    (unsigned long)g_npu.irq_initial,
    (unsigned long)g_npu.irq_enable,
    (unsigned long)g_npu.irq_count,
    (unsigned long)g_npu.irq_last,
    g_npu.memory_checkpoint,
    (unsigned long long)memory.base,
    (unsigned long long)memory.end,
    (unsigned long)memory.total,
    (unsigned long)memory.used,
    memory.allocations,
    memory.low32,
    memory.guards,
    memory.selftest,
    g_npu.submit_checkpoint,
    (unsigned long)g_npu.submit_physical,
    (unsigned long)g_npu.submit_size,
    (unsigned long)g_npu.submit_mmu,
    (unsigned long)g_npu.submit_irq_before,
    (unsigned long)g_npu.submit_irq_after,
    (unsigned long)g_npu.submit_irq_value,
    (unsigned long)g_npu.submit_polls,
    (unsigned long)g_npu.submit_idle,
    g_npu.submit_guards,
    g_npu.submit_recovery,
    g_npu.mmutest_checkpoint,
    (unsigned long)g_npu.mmutest_mtlb,
    (unsigned long)g_npu.mmutest_stlb,
    (unsigned long)g_npu.mmutest_command,
    (unsigned long)g_npu.mmutest_virtual,
    (unsigned long)g_npu.mmutest_pd_entry,
    (unsigned long)g_npu.mmutest_control,
    (unsigned long)g_npu.mmutest_irq_before,
    (unsigned long)g_npu.mmutest_irq_after,
    (unsigned long)g_npu.mmutest_irq_value,
    (unsigned long)g_npu.mmutest_polls,
    (unsigned long)g_npu.mmutest_idle,
    g_npu.mmutest_guards,
    g_npu.mmutest_recovery,
    g_npu.checkpoint);

  if ((size_t)filep->f_pos >= length)
    {
      return 0;
    }

  copy = length - (size_t)filep->f_pos;
  if (copy > buflen)
    {
      copy = buflen;
    }

  memcpy(buffer, report + filep->f_pos, copy);
  filep->f_pos += copy;
  return (ssize_t)copy;
}

static ssize_t a733_npu_write(struct file *filep, const char *buffer,
                              size_t buflen)
{
  char command[24];
  size_t length;
  int ret;

  (void)filep;
  if (buflen == 0 || buflen >= sizeof(command))
    {
      return -EINVAL;
    }

  memcpy(command, buffer, buflen);
  command[buflen] = '\0';
  length = buflen;
  while (length > 0 && (command[length - 1] == '\n' ||
                        command[length - 1] == '\r' ||
                        command[length - 1] == ' ' ||
                        command[length - 1] == '\t'))
    {
      command[--length] = '\0';
    }

  if (strcmp(command, "submit") == 0)
    {
      ret = a733_npu_submit_event();
      syslog(ret < 0 ? LOG_ERR : LOG_INFO,
             "A733 NPU: VIP2 EVENT+END submit %s (%d), phys=%08lx "
             "irq=%lu->%lu value=%08lx idle=%08lx guards=%d recovery=%d\n",
             ret < 0 ? "failed" : "passed", ret,
             (unsigned long)g_npu.submit_physical,
             (unsigned long)g_npu.submit_irq_before,
             (unsigned long)g_npu.submit_irq_after,
             (unsigned long)g_npu.submit_irq_value,
             (unsigned long)g_npu.submit_idle,
             g_npu.submit_guards, g_npu.submit_recovery);
    }
  else if (strcmp(command, "mmutest") == 0)
    {
      ret = a733_npu_mmu_test();
      syslog(ret < 0 ? LOG_ERR : LOG_INFO,
             "A733 NPU: VIP2 PD-MMU translated submit %s (%d), "
             "va=%08lx phys=%08lx pd=%08lx irq=%lu->%lu value=%08lx "
             "idle=%08lx guards=%d recovery=%d\n",
             ret < 0 ? "failed" : "passed", ret,
             (unsigned long)g_npu.mmutest_virtual,
             (unsigned long)g_npu.mmutest_command,
             (unsigned long)g_npu.mmutest_pd_entry,
             (unsigned long)g_npu.mmutest_irq_before,
             (unsigned long)g_npu.mmutest_irq_after,
             (unsigned long)g_npu.mmutest_irq_value,
             (unsigned long)g_npu.mmutest_idle,
             g_npu.mmutest_guards, g_npu.mmutest_recovery);
    }
  else
    {
      return -EINVAL;
    }
  return ret < 0 ? ret : (ssize_t)buflen;
}

static const struct file_operations g_a733_npu_fops =
{
  .read = a733_npu_read,
  .write = a733_npu_write,
};

int a733_npu_initialize(void)
{
  int ret;

  memset(&g_npu, 0, sizeof(g_npu));
  g_npu.checkpoint = -EAGAIN;
  g_npu.submit_checkpoint = -EAGAIN;
  g_npu.submit_recovery = -EAGAIN;
  g_npu.submit_guards = -EAGAIN;
  g_npu.mmutest_checkpoint = -EAGAIN;
  g_npu.mmutest_recovery = -EAGAIN;
  g_npu.mmutest_guards = -EAGAIN;
  g_submit_allocated = false;
  g_mmu_allocated = false;
  g_submit_busy = false;

  ret = register_driver("/dev/a733-npu", &g_a733_npu_fops, 0644, NULL);
  if (ret < 0)
    {
      return ret;
    }

  g_npu.checkpoint = a733_npu_probe();
  syslog(g_npu.checkpoint < 0 ? LOG_ERR : LOG_INFO,
         "A733 NPU: VIP2 power/clock/identity checkpoint %s (%d), "
         "id=%08lx/%08lx/%08lx/%08lx\n",
         g_npu.checkpoint < 0 ? "failed" : "passed",
         g_npu.checkpoint,
         (unsigned long)g_npu.version1,
         (unsigned long)g_npu.version2,
         (unsigned long)g_npu.date,
         (unsigned long)g_npu.cid);
  if (g_npu.checkpoint == OK)
    {
      ret = a733_npu_runtime_initialize();
      if (ret < 0)
        {
          syslog(LOG_ERR, "A733 NPU: /dev/npu0 runtime init failed (%d)\n",
                 ret);
          return ret;
        }
    }

  return g_npu.checkpoint;
}

#endif
