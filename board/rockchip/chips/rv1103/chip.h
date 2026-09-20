/****************************************************************************
 * vendor/rockchip/chips/rv1103/chip.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __VENDOR_ROCKCHIP_CHIPS_RV1103_CHIP_H
#define __VENDOR_ROCKCHIP_CHIPS_RV1103_CHIP_H

#include <nuttx/config.h>

/* RV1103 Cortex-A7 private peripheral region. */

#define CHIP_MPCORE_VBASE     0xff1f0000
#define MPCORE_ICD_OFFSET     0x00001000
#define MPCORE_ICC_OFFSET     0x00002000

#define PGTABLE_SIZE          0x00004000
#define PGTABLE_BASE_PADDR    (CONFIG_RAM_START + CONFIG_RAM_SIZE - \
                               PGTABLE_SIZE * CONFIG_NCPUS)
#define PGTABLE_BASE_VADDR    PGTABLE_BASE_PADDR

#undef CONFIG_RAM_END
#define CONFIG_RAM_END        PGTABLE_BASE_PADDR

#define NUTTX_TEXT_PADDR      (CONFIG_RAM_START & 0xfff00000)
#define NUTTX_TEXT_VADDR      (CONFIG_RAM_VSTART & 0xfff00000)
#define NUTTX_TEXT_PEND       ((CONFIG_RAM_START + CONFIG_RAM_SIZE + \
                                0x000fffff) & 0xfff00000)
#define NUTTX_TEXT_SIZE       (NUTTX_TEXT_PEND - NUTTX_TEXT_PADDR)

#define NUTTX_RAM_PADDR       NUTTX_TEXT_PADDR
#define NUTTX_RAM_VADDR       NUTTX_TEXT_VADDR
#define NUTTX_RAM_PEND        NUTTX_TEXT_PEND
#define NUTTX_RAM_SIZE        NUTTX_TEXT_SIZE

#endif /* __VENDOR_ROCKCHIP_CHIPS_RV1103_CHIP_H */
