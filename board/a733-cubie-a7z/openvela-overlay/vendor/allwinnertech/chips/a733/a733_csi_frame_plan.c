/* SPDX-License-Identifier: Apache-2.0 */
#include "a733_csi_frame_plan.h"
#include <errno.h>

/* Facts cross-checked against Allwinner vin_core.c / vin_video.c and
 * dma140_reg.c/_i.h: Bayer10 has 16-bit memory depth, line length width*2,
 * allocation dimensions rounded to 16, buffer-address field is 32 bits
 * containing device-address >> 2. Independent arithmetic, no GPL table or
 * implementation copied. Actual revision/format still needs hardware proof.
 */
int a733_csi_raw10_frame_plan(uint32_t width, uint32_t height,
                             uint64_t allocation_iova, size_t mapped_bytes,
                             struct a733_csi_frame_plan *out)
{
  if (!out || width != 1920 || height != 1080) return -EINVAL;
  if (!allocation_iova || (allocation_iova & 63u)) return -EINVAL;
  uint64_t stride = ((uint64_t)width + 15) / 16 * 16 * 2;
  uint64_t rows = ((uint64_t)height + 15) / 16 * 16;
  uint64_t capacity = stride * rows;
  uint64_t total = capacity + 128; /* Whole owned 64B guards, not image data. */
  if (total > UINT32_MAX || mapped_bytes < total) return -EMSGSIZE;
  const uint64_t aperture = UINT64_C(1) << 34;
  if (allocation_iova >= aperture || total > aperture - allocation_iova)
    return -ERANGE;
  uint64_t payload = allocation_iova + 64;
  struct a733_csi_frame_plan p =
  {
    .allocation_iova = allocation_iova, .payload_iova = payload,
    .address_word = (uint32_t)(payload >> 2), .width = width, .height = height,
    .allocation_rows = (uint32_t)rows, .stride = (uint32_t)stride,
    .active_bytes = (uint32_t)(stride * height),
    .payload_capacity = (uint32_t)capacity, .allocation_bytes = (uint32_t)total,
    .prefix_guard = 64, .suffix_guard = 64
  };
  *out = p;
  return 0;
}
