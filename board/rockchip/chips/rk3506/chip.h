/****************************************************************************
 * vendor/rockchip/chips/rk3506/chip.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __VENDOR_ROCKCHIP_CHIPS_RK3506_CHIP_H
#define __VENDOR_ROCKCHIP_CHIPS_RK3506_CHIP_H

#include <nuttx/config.h>

/* RK3506 Cortex-A7 private peripheral region.
 *
 *   GIC distributor: 0xff581000
 *   GIC CPU interface: 0xff582000
 */

#define CHIP_MPCORE_VBASE     0xff580000
#define MPCORE_ICD_OFFSET     0x00001000
#define MPCORE_ICC_OFFSET     0x00002000

/* Keep one 16-KiB L1 page table per enabled CPU at the top of the memory
 * region owned by openvela.  The initial configuration is UP, but expressing
 * the reservation in terms of CONFIG_NCPUS keeps the layout SMP-ready.
 */

#define PGTABLE_SIZE          0x00004000
#define PGTABLE_BASE_PADDR    (CONFIG_RAM_START + CONFIG_RAM_SIZE - \
                               PGTABLE_SIZE * CONFIG_NCPUS)
#define PGTABLE_BASE_VADDR    PGTABLE_BASE_PADDR

#undef CONFIG_RAM_END
#define CONFIG_RAM_END        PGTABLE_BASE_PADDR

/* U-Boot loads a flat image into DDR.  Physical and virtual addresses are
 * identical, and the initial section mapping covers the whole region owned
 * by openvela.
 */

#define NUTTX_TEXT_PADDR      (CONFIG_RAM_START & 0xfff00000)
#define NUTTX_TEXT_VADDR      (CONFIG_RAM_VSTART & 0xfff00000)
#define NUTTX_TEXT_PEND       ((CONFIG_RAM_START + CONFIG_RAM_SIZE + \
                                0x000fffff) & 0xfff00000)
#define NUTTX_TEXT_SIZE       (NUTTX_TEXT_PEND - NUTTX_TEXT_PADDR)

#define NUTTX_RAM_PADDR       NUTTX_TEXT_PADDR
#define NUTTX_RAM_VADDR       NUTTX_TEXT_VADDR
#define NUTTX_RAM_PEND        NUTTX_TEXT_PEND
#define NUTTX_RAM_SIZE        NUTTX_TEXT_SIZE

#endif /* __VENDOR_ROCKCHIP_CHIPS_RK3506_CHIP_H */
