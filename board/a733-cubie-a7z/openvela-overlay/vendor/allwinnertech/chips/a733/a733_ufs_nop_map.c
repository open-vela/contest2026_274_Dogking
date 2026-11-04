/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#ifdef CONFIG_A733_UFS_DMA_NOP
#include <errno.h>
#include <stdint.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>
#include "a733_ufs_nop.h"

int a733_ufs_nop_translate(void *context, uintptr_t address, uint64_t *physical)
{
  uint64_t read_result;
  uint64_t write_result;
  uint64_t saved;
  irqstate_t flags;
  (void)context;
  if (physical == NULL || (address & 4095) != 0) return -EINVAL;
  flags = enter_critical_section();
  __asm__ volatile("mrs %0, par_el1" : "=r"(saved));
  __asm__ volatile("at s1e1r, %1\n\tisb\n\tmrs %0, par_el1"
                   : "=r"(read_result) : "r"(address) : "memory");
  __asm__ volatile("at s1e1w, %1\n\tisb\n\tmrs %0, par_el1"
                   : "=r"(write_result) : "r"(address) : "memory");
  __asm__ volatile("msr par_el1, %0\n\tisb" : : "r"(saved) : "memory");
  leave_critical_section(flags);
  if ((read_result & 1) || (write_result & 1)) return -EFAULT;
  if ((read_result & UINT64_C(0x0000fffffffff000)) !=
      (write_result & UINT64_C(0x0000fffffffff000))) return -EFAULT;
  *physical = read_result & UINT64_C(0x0000fffffffff000);
  return 0;
}
#endif
