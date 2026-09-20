/****************************************************************************
 * vendor/rockchip/chips/rk3506/rk3506_memorymap.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <sys/param.h>

#include "mmu.h"
#include "rk3506_memorymap.h"

#define RK3506_MMIO_PSECTION  0xff000000
#define RK3506_MMIO_VSECTION  RK3506_MMIO_PSECTION
#define RK3506_MMIO_SIZE      0x01000000
#define RK3506_MMIO_NSECTIONS (RK3506_MMIO_SIZE >> 20)

/* DDR is mapped by arm_head.S.  This identity mapping supplies the MMIO
 * window containing UART0, the GIC, GRF, CRU, GPIO and the other RK3506
 * peripherals required by later drivers.
 */

static const struct section_mapping_s g_section_mapping[] =
{
  {
    RK3506_MMIO_PSECTION,
    RK3506_MMIO_VSECTION,
    MMU_IOFLAGS,
    RK3506_MMIO_NSECTIONS
  }
};

int rk3506_setupmappings(void)
{
  mmu_l1_map_regions(g_section_mapping, nitems(g_section_mapping));
  return 0;
}
