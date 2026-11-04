/****************************************************************************
 * vendor/allwinnertech/chips/a733/include/chip.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __VENDOR_ALLWINNERTECH_CHIPS_A733_INCLUDE_CHIP_H
#define __VENDOR_ALLWINNERTECH_CHIPS_A733_INCLUDE_CHIP_H

#include <nuttx/config.h>
#include <stdint.h>

#define KB(x) ((x) << 10)
#define MB(x) (KB(x) << 10)
#define GB(x) (MB(UINT64_C(x)) << 10)

/* A733 GICv3, from sun60iw2p1.dtsi. */

#define CONFIG_GICD_BASE          0x03400000
#define CONFIG_GICR_BASE          0x03460000
#define CONFIG_GICR_OFFSET        0x00020000

/* Initial safe memory window.
 *
 * Physical DRAM begins at 0x40000000 and is 4 GiB on the target board.
 * The official firmware reserves 0x48000000..0x48ffffff for BL31.  Stage 1
 * therefore exposes only the window containing the image and ending exactly
 * before BL31.  Later configurations may add the remaining banks explicitly.
 */

#define CONFIG_RAMBANK1_ADDR      CONFIG_RAM_START
#define CONFIG_RAMBANK1_SIZE      CONFIG_RAM_SIZE

/* Cubie A7Z has 4 GiB at 0x40000000..0x13ffffffff.  Keep the firmware's
 * 16 MiB BL31 carveout out of both the MMU-owned heap and the allocator.
 * The first 2 MiB below CONFIG_RAM_START contains the boot handoff area and
 * is intentionally not reclaimed during board bring-up.
 */

#define CONFIG_RAMBANK2_ADDR      UINT64_C(0x49000000)
#define CONFIG_RAMBANK2_END       UINT64_C(0x140000000)
#define CONFIG_RAMBANK2_SIZE      (CONFIG_RAMBANK2_END - \
                                   CONFIG_RAMBANK2_ADDR)

/* All currently required MMIO is below the DRAM aperture. */

#define CONFIG_DEVICEIO_BASEADDR  0x00000000
#define CONFIG_DEVICEIO_SIZE      GB(1)

#define CONFIG_LOAD_BASE          0x40200000

#define ARMV8A_DCACHE_LINESIZE    64
#define ARMV8A_ICACHE_LINESIZE    64

#define MPID_TO_CLUSTER_ID(mpid)  ((mpid) & ~UINT64_C(0xff))
#define A733_MPID_TO_CPU(mpid)    (((uint64_t)(mpid) >> 8) & \
                                   UINT64_C(0xff))

#ifdef __ASSEMBLY__

.macro get_cpu_id xreg0
  mrs  \xreg0, mpidr_el1
  ubfx \xreg0, \xreg0, #8, #8
.endm

#endif

#endif /* __VENDOR_ALLWINNERTECH_CHIPS_A733_INCLUDE_CHIP_H */
