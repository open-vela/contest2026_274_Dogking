/****************************************************************************
 * vendor/rockchip/chips/rv1103/include/rv1103_dma.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __VENDOR_ROCKCHIP_CHIPS_RV1103_INCLUDE_RV1103_DMA_H
#define __VENDOR_ROCKCHIP_CHIPS_RV1103_INCLUDE_RV1103_DMA_H

#include <nuttx/config.h>

#ifdef CONFIG_RV1103_PL330_DMA

#include <stddef.h>

int rv1103_dma_initialize(void);
int rv1103_dma_memcpy(void *dst, const void *src, size_t len);

#endif
#endif /* __VENDOR_ROCKCHIP_CHIPS_RV1103_INCLUDE_RV1103_DMA_H */
