/* SPDX-License-Identifier: Apache-2.0 */

#include <nuttx/config.h>

#include <errno.h>
#include <inttypes.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include "aipetllm_sequence.h"

int aipetllm_cpu_checkpoint(void);
int aipetllm_forward_ram_checkpoint(const char *path, uint32_t token_id,
                                   unsigned int cpu_mask);
int aipetllm_forward_fast_checkpoint(const char *path, uint32_t token_id,
                                    unsigned int cpu_mask);
int aipetllm_forward2_ram_checkpoint(const char *path, uint32_t first_token,
                                     uint32_t second_token,
                                     unsigned int cpu_mask);
int aipetllm_forward2_fast_checkpoint(const char *path, uint32_t first_token,
                                      uint32_t second_token,
                                      unsigned int cpu_mask);

static int forward_fast(const char *path, uint32_t token_id,
                        unsigned int cpu_mask, int two_tokens,
                        uint32_t second_token,
                        struct aipetllm_sequence_s *sequence)
{
  cpu_set_t original = 0;
  cpu_set_t selected = 0;
  int count = 0;
  int cpu;
  int result = 1;

  if (CONFIG_SMP_NCPUS != 8 || cpu_mask == 0 || (cpu_mask & ~0xffu) ||
      sched_getaffinity(0, sizeof(original), &original) < 0)
    {
      fputs("forwardfast: verified eight-CPU platform required\n", stderr);
      return 1;
    }

  for (cpu = 0; cpu < 8; cpu++)
    {
      if (cpu_mask & (1u << cpu))
        {
          CPU_SET(cpu, &selected);
          count++;
        }
    }
  if (sched_setaffinity(0, sizeof(selected), &selected) < 0)
    {
      fputs("forwardfast: cannot select requested cores\n", stderr);
      return 1;
    }

  printf("forwardfast: cores-mask=%02x workers=%d; "
         "A76:A55 row weight=3:1; system scheduling unchanged\n",
         cpu_mask, count);
  result = sequence != NULL ?
    aipetllm_sequence_ram_checkpoint(path, sequence, cpu_mask) : two_tokens ?
    aipetllm_forward2_ram_checkpoint(path, token_id, second_token, cpu_mask) :
    aipetllm_forward_ram_checkpoint(path, token_id, cpu_mask);

  if (sched_setaffinity(0, sizeof(original), &original) < 0)
    {
      fputs("forwardfast: affinity restore failed\n", stderr);
      result = 1;
    }

  return result;
}

int aipetllm_forward_fast_checkpoint(const char *path, uint32_t token_id,
                                    unsigned int cpu_mask)
{
  return forward_fast(path, token_id, cpu_mask, 0, 0, NULL);
}

int aipetllm_forward2_fast_checkpoint(const char *path, uint32_t first_token,
                                     uint32_t second_token,
                                     unsigned int cpu_mask)
{
  return forward_fast(path, first_token, cpu_mask, 1, second_token, NULL);
}

int aipetllm_sequence_fast_checkpoint(const char *path,
                                      struct aipetllm_sequence_s *sequence,
                                      unsigned int cpu_mask)
{
  if (sequence == NULL || sequence->input_count == 0 ||
      sequence->input_count > AIPETLLM_INPUT_LIMIT)
    {
      return 1;
    }

  return forward_fast(path, sequence->input[0], cpu_mask, 0, 0, sequence);
}

/* Diagnostic only: move this task, read identity, run the same bounded integer
 * workload on each CPU, then restore its original affinity. No clocks, power
 * rails, interrupt routing or LLM compute policy are changed here.
 */

int aipetllm_cpu_checkpoint(void)
{
  cpu_set_t original = 0;
  uint32_t reference = 0;
  int failed = 0;
  int target;

  if (sched_getaffinity(0, sizeof(original), &original) < 0)
    {
      printf("cpucheck: getaffinity failed errno=%d\n", errno);
      return 1;
    }

  printf("cpucheck: configured=%d original-affinity=%08lx\n",
         CONFIG_SMP_NCPUS, (unsigned long)original);

  for (target = 0; target < CONFIG_SMP_NCPUS; target++)
    {
      cpu_set_t selected = 0;
      uint64_t mpidr;
      uint64_t midr;
      uint32_t part;
      volatile uint32_t checksum = UINT32_C(0x12345678);
      unsigned int iteration;
      int actual;
      int wait;
      int status;

      CPU_SET(target, &selected);
      if (sched_setaffinity(0, sizeof(selected), &selected) < 0)
        {
          printf("cpu[%d]: setaffinity failed errno=%d\n", target, errno);
          failed = 1;
          break;
        }

      for (wait = 0; wait < 100 && sched_getcpu() != target; wait++)
        {
          usleep(1000);
        }

      actual = sched_getcpu();
      if (actual != target)
        {
          printf("cpu[%d]: migration failed actual=%d\n", target, actual);
          failed = 1;
          break;
        }

      __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
      __asm__ volatile("mrs %0, midr_el1" : "=r"(midr));
      part = (uint32_t)((midr >> 4) & UINT64_C(0xfff));

      for (iteration = 0; iteration < 100000; iteration++)
        {
          checksum = checksum * UINT32_C(1664525) + UINT32_C(1013904223);
        }

      if (target == 0)
        {
          reference = checksum;
        }

      status = ((mpidr >> 8) & UINT64_C(0xff)) == (uint64_t)target &&
               part == (target < 6 ? UINT32_C(0xd05) : UINT32_C(0xd0b)) &&
               checksum == reference && sched_getcpu() == target ? 0 : 1;
      printf("cpu[%d]: actual=%d mpidr=%016" PRIx64
             " midr=%016" PRIx64 " part=%03" PRIx32
             " type=%s checksum=%08" PRIx32 " checkpoint=%d\n",
             target, actual, mpidr, midr, part,
             part == 0xd05 ? "Cortex-A55" :
             part == 0xd0b ? "Cortex-A76" : "unknown",
             (uint32_t)checksum, status);
      failed |= status;
    }

  if (sched_setaffinity(0, sizeof(original), &original) < 0)
    {
      printf("cpucheck: restore-affinity failed errno=%d\n", errno);
      return 1;
    }

  printf("cpucheck: %s; original affinity restored; LLM parallelism pending\n",
         failed ? "FAILED" : "passed");
  return failed;
}
