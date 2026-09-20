/****************************************************************************
 * vendor/allwinnertech/chips/a733/a733_boot.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <nuttx/kmalloc.h>
#include <nuttx/kthread.h>

#include <arch/chip/chip.h>

#include "arm64_internal.h"
#include "arm64_mmu.h"

extern void a733_board_initialize(void);
int a733_compute_service_start(int (*entry)(int, char **));

/* Platform-owned service lifetime: application code does not include private
 * kernel headers or manipulate scheduler TCBs. The service owns its pthreads.
 */

int a733_compute_service_start(int (*entry)(int, char **))
{
  return kthread_create("a733-llm-pool", 100, 8192, entry, NULL);
}

#ifdef CONFIG_ARCH_HAVE_MULTICPU
uint64_t arm64_get_mpid(int cpu)
{
  /* A733 numbers all eight application CPUs in MPIDR Aff1 while Aff0 is
   * zero.  This is the same mapping published by the official sun60iw2p1
   * Linux DTS and reported by the GICR_TYPER affinity fields.
   */

  return (uint64_t)(unsigned int)cpu << 8;
}

int arm64_get_cpuid(uint64_t mpid)
{
  return (int)A733_MPID_TO_CPU(mpid);
}
#endif

void a733_boot_marker(const char *marker)
{
#ifdef CONFIG_ARCH_EARLY_PRINT
  while (*marker != '\0')
    {
      arm64_lowputc(*marker++);
    }
#else
  (void)marker;
#endif
}

static const struct arm_mmu_region g_mmu_regions[] =
{
  MMU_REGION_FLAT_ENTRY("DEVICE",
                        CONFIG_DEVICEIO_BASEADDR,
                        CONFIG_DEVICEIO_SIZE,
                        MT_DEVICE_NGNRNE | MT_RW | MT_SECURE),

  MMU_REGION_FLAT_ENTRY("DRAM_STAGE1",
                        CONFIG_RAMBANK1_ADDR,
                        CONFIG_RAMBANK1_SIZE,
                        MT_NORMAL | MT_RW | MT_SECURE),

  MMU_REGION_FLAT_ENTRY("DRAM_MAIN",
                        CONFIG_RAMBANK2_ADDR,
                        CONFIG_RAMBANK2_SIZE,
                        MT_NORMAL | MT_RW | MT_SECURE),
};

const struct arm_mmu_config g_mmu_config =
{
  .num_regions = nitems(g_mmu_regions),
  .mmu_regions = g_mmu_regions,
};

void arm64_el_init(void)
{
  /* BL31 enters the image at non-secure EL1h and has already initialized
   * CNTFRQ_EL0, DDR and the system clocks.  Do not disturb that handoff.
   */
}

void arm64_chip_boot(void)
{
  arm64_mmu_init(true);

#ifdef CONFIG_ARM64_PSCI
  /* The official boot chain enters through TF-A BL31, whose PSCI conduit is
   * SMC.  This enables the standard NuttX reboot and poweroff board calls.
   */

  arm64_psci_init("smc");
#endif

  a733_board_initialize();

#ifdef USE_EARLYSERIALINIT
  arm64_earlyserialinit();
#endif

  a733_boot_marker("A7Z1\r\n");
}

#if CONFIG_MM_REGIONS > 1
void arm64_addregion(void)
{
  /* Region 0 is allocated by the common ARM64 heap logic and ends exactly
   * at the BL31 carveout.  Add the remainder of the 4 GiB Cubie A7Z DRAM as
   * a discontiguous second region after that carveout.
   */

  kumm_addregion((FAR void *)(uintptr_t)CONFIG_RAMBANK2_ADDR,
                 (size_t)CONFIG_RAMBANK2_SIZE);
}
#endif

#if defined(CONFIG_NET) && !defined(CONFIG_NETDEV_LATEINIT)
void arm64_netinitialize(void)
{
}
#endif
