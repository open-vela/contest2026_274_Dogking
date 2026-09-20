/****************************************************************************
 * vendor/allwinnertech/chips/a733/a733_npu_memory.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_A733_NPU_CHECKPOINT

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/cache.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>

#include "a733_npu_memory.h"

#define A733_NPU_ARENA_SIZE       (32u * 1024u * 1024u)
#define A733_NPU_CACHE_LINE       64u
#define A733_NPU_GUARD_SIZE       A733_NPU_CACHE_LINE
#define A733_NPU_MAX_BUFFERS      32u
#define A733_NPU_HEAD_PATTERN     UINT8_C(0xa5)
#define A733_NPU_TAIL_PATTERN     UINT8_C(0x5a)

struct a733_npu_record_s
{
  size_t head;
  size_t payload;
  size_t size;
  size_t tail;
  bool active;
};

/* .bss is in the stage-1 identity-mapped bank below the BL31 carveout.  A
 * linker assertion already prevents this arena from crossing 0x48000000.
 */

static uint8_t g_npu_arena[A733_NPU_ARENA_SIZE]
  __attribute__((aligned(4096)));
static struct a733_npu_record_s g_records[A733_NPU_MAX_BUFFERS];
static size_t g_used;
static unsigned int g_allocations;
static int g_selftest = -EAGAIN;
static int g_guard_status = -EAGAIN;

static size_t a733_align_up(size_t value, size_t align)
{
  return (value + align - 1u) & ~(align - 1u);
}

int a733_npu_mem_check_guards(void)
{
  unsigned int slot;
  size_t i;

  for (slot = 0; slot < A733_NPU_MAX_BUFFERS; slot++)
    {
      if (!g_records[slot].active)
        {
          continue;
        }

      for (i = 0; i < A733_NPU_GUARD_SIZE; i++)
        {
          if (g_npu_arena[g_records[slot].head + i] !=
              A733_NPU_HEAD_PATTERN ||
              g_npu_arena[g_records[slot].tail + i] !=
              A733_NPU_TAIL_PATTERN)
            {
              g_guard_status = -EFAULT;
              return g_guard_status;
            }
        }
    }

  g_guard_status = OK;
  return OK;
}

int a733_npu_mem_alloc(size_t size, size_t align,
                       struct a733_npu_buffer_s *buffer)
{
  irqstate_t flags;
  uintptr_t physical;
  size_t payload;
  size_t rounded;
  size_t head;
  size_t tail;
  size_t end;
  unsigned int slot;

  if (buffer == NULL || size == 0)
    {
      return -EINVAL;
    }

  if (align < A733_NPU_CACHE_LINE)
    {
      align = A733_NPU_CACHE_LINE;
    }

  if ((align & (align - 1u)) != 0)
    {
      return -EINVAL;
    }

  rounded = a733_align_up(size, A733_NPU_CACHE_LINE);
  flags = enter_critical_section();
  if (g_allocations >= A733_NPU_MAX_BUFFERS)
    {
      leave_critical_section(flags);
      return -ENOSPC;
    }

  slot = g_allocations;
  if (g_used > sizeof(g_npu_arena) - A733_NPU_GUARD_SIZE)
    {
      leave_critical_section(flags);
      return -ENOMEM;
    }

  /* Align the actual DMA/physical address, not just the byte offset within
   * the arena.  The arena itself is guaranteed 4 KiB alignment, but callers
   * such as the VIP2 4K-STLB require 16 KiB alignment.  Aligning only g_used
   * silently inherits the arena's modulo-16-KiB displacement.
   */

  payload = a733_align_up((uintptr_t)&g_npu_arena[g_used +
                          A733_NPU_GUARD_SIZE], align) -
            (uintptr_t)g_npu_arena;
  head = payload - A733_NPU_GUARD_SIZE;
  tail = payload + rounded;
  end = tail + A733_NPU_GUARD_SIZE;
  if (end > sizeof(g_npu_arena))
    {
      leave_critical_section(flags);
      return -ENOMEM;
    }

  physical = (uintptr_t)&g_npu_arena[payload];
  if (physical > UINT32_MAX || rounded - 1u > UINT32_MAX - physical)
    {
      leave_critical_section(flags);
      return -ERANGE;
    }

  memset(&g_npu_arena[head], A733_NPU_HEAD_PATTERN,
         A733_NPU_GUARD_SIZE);
  memset(&g_npu_arena[tail], A733_NPU_TAIL_PATTERN,
         A733_NPU_GUARD_SIZE);
  memset(&g_npu_arena[payload], 0, rounded);

  g_records[slot].head = head;
  g_records[slot].payload = payload;
  g_records[slot].size = rounded;
  g_records[slot].tail = tail;
  g_records[slot].active = true;
  g_used = end;
  g_allocations++;

  buffer->cpu = &g_npu_arena[payload];
  buffer->physical = (uint32_t)physical;
  buffer->size = rounded;
  buffer->slot = slot;
  leave_critical_section(flags);
  return OK;
}

void a733_npu_mem_clean(const struct a733_npu_buffer_s *buffer)
{
  if (buffer != NULL && buffer->cpu != NULL)
    {
      up_clean_dcache((uintptr_t)buffer->cpu,
                      (uintptr_t)buffer->cpu + buffer->size);
    }
}

void a733_npu_mem_invalidate(const struct a733_npu_buffer_s *buffer)
{
  if (buffer != NULL && buffer->cpu != NULL)
    {
      up_invalidate_dcache((uintptr_t)buffer->cpu,
                           (uintptr_t)buffer->cpu + buffer->size);
    }
}

int a733_npu_mem_initialize(void)
{
  struct a733_npu_buffer_s test;
  uint32_t *words;
  size_t count;
  size_t i;
  int ret;

  memset(g_records, 0, sizeof(g_records));
  g_used = 0;
  g_allocations = 0;
  g_selftest = -EAGAIN;
  g_guard_status = -EAGAIN;

  if ((uintptr_t)g_npu_arena > UINT32_MAX ||
      sizeof(g_npu_arena) - 1u > UINT32_MAX - (uintptr_t)g_npu_arena)
    {
      g_selftest = -ERANGE;
      return g_selftest;
    }

  ret = a733_npu_mem_alloc(64u * 1024u, 4096u, &test);
  if (ret < 0)
    {
      g_selftest = ret;
      return ret;
    }

  words = (uint32_t *)test.cpu;
  count = test.size / sizeof(uint32_t);
  for (i = 0; i < count; i++)
    {
      words[i] = UINT32_C(0x6e707500) ^ (uint32_t)i;
    }

  a733_npu_mem_clean(&test);
  a733_npu_mem_invalidate(&test);
  for (i = 0; i < count; i++)
    {
      if (words[i] != (UINT32_C(0x6e707500) ^ (uint32_t)i))
        {
          g_selftest = -EIO;
          return g_selftest;
        }
    }

  ret = a733_npu_mem_check_guards();
  if (ret < 0)
    {
      g_selftest = ret;
      return ret;
    }

  /* The test buffer is not exposed to later layers.  Rewind the arena after
   * proving address width, alignment, cache maintenance and guards.
   */

  memset(g_records, 0, sizeof(g_records));
  g_used = 0;
  g_allocations = 0;
  g_guard_status = OK;
  g_selftest = OK;
  return OK;
}

void a733_npu_mem_get_stats(struct a733_npu_mem_stats_s *stats)
{
  irqstate_t flags;

  if (stats == NULL)
    {
      return;
    }

  flags = enter_critical_section();
  stats->base = (uintptr_t)g_npu_arena;
  stats->end = (uintptr_t)g_npu_arena + sizeof(g_npu_arena);
  stats->total = sizeof(g_npu_arena);
  stats->used = g_used;
  stats->allocations = g_allocations;
  stats->low32 = stats->end - 1u <= UINT32_MAX ? OK : -ERANGE;
  stats->selftest = g_selftest;
  stats->guards = g_guard_status;
  leave_critical_section(flags);
}

#endif
