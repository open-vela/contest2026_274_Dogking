/****************************************************************************
 * vendor/rockchip/chips/rv1103/rv1103_memorymap.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <sys/param.h>

#include "mmu.h"
#include "rv1103_memorymap.h"

#define RV1103_MMIO_PSECTION  0xff000000
#define RV1103_MMIO_VSECTION  RV1103_MMIO_PSECTION
#define RV1103_MMIO_SIZE      0x01000000
#define RV1103_MMIO_NSECTIONS (RV1103_MMIO_SIZE >> 20)

static const struct section_mapping_s g_section_mapping[] =
{
  {
    RV1103_MMIO_PSECTION,
    RV1103_MMIO_VSECTION,
    MMU_IOFLAGS,
    RV1103_MMIO_NSECTIONS
  }
};

int rv1103_setupmappings(void)
{
  mmu_l1_map_regions(g_section_mapping, nitems(g_section_mapping));
  return 0;
}
