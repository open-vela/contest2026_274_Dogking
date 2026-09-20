/****************************************************************************
 * vendor/allwinnertech/chips/a733/include/a733_npu_memory.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __VENDOR_ALLWINNERTECH_CHIPS_A733_INCLUDE_A733_NPU_MEMORY_H
#define __VENDOR_ALLWINNERTECH_CHIPS_A733_INCLUDE_A733_NPU_MEMORY_H

#include <stddef.h>
#include <stdint.h>

struct a733_npu_buffer_s
{
  void *cpu;
  uint32_t physical;
  size_t size;
  unsigned int slot;
};

struct a733_npu_mem_stats_s
{
  uintptr_t base;
  uintptr_t end;
  size_t total;
  size_t used;
  unsigned int allocations;
  int low32;
  int selftest;
  int guards;
};

int a733_npu_mem_initialize(void);
int a733_npu_mem_alloc(size_t size, size_t align,
                       struct a733_npu_buffer_s *buffer);
void a733_npu_mem_clean(const struct a733_npu_buffer_s *buffer);
void a733_npu_mem_invalidate(const struct a733_npu_buffer_s *buffer);
int a733_npu_mem_check_guards(void);
void a733_npu_mem_get_stats(struct a733_npu_mem_stats_s *stats);

#endif
