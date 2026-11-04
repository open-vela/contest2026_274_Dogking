/* SPDX-License-Identifier: Apache-2.0 */
#ifndef A733_CSI_FRAME_PLAN_H
#define A733_CSI_FRAME_PLAN_H
#include <stddef.h>
#include <stdint.h>

/* Numerical candidate plan, NOT an allocator, DMA mapping or register
 * programmer. Device addresses are CSI-master IOVAs, never CPU pointers or
 * an NPU master's mappings. The broker must independently prove ownership,
 * address translation, cache maintenance and the negotiated memory format.
 */
struct a733_csi_frame_plan
{
  uint64_t allocation_iova;
  uint64_t payload_iova;
  uint32_t address_word; /* dma140 32-bit field encodes address >> 2. */
  uint32_t width, height, allocation_rows, stride;
  uint32_t active_bytes, payload_capacity, allocation_bytes;
  uint32_t prefix_guard, suffix_guard;
};

/* Initial 1920x1080 RAW10-in-LE16 reference only. 64-byte CPU cache/guard
 * alignment and conservative 16-row allocation. A successful calculation
 * does not prove that a live CSI block actually produced this memory layout.
 */
int a733_csi_raw10_frame_plan(uint32_t width, uint32_t height,
                             uint64_t allocation_iova, size_t mapped_bytes,
                             struct a733_csi_frame_plan *out);
#endif
