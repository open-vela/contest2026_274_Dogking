/* SPDX-License-Identifier: Apache-2.0 */
#ifndef A733_TENSOR_PLAN_H
#define A733_TENSOR_PLAN_H
#include <stddef.h>
#include <stdint.h>
#include <nuttx/npu/a733_tensor.h>

#define A733_TENSOR_POOL A733_TENSOR_DMA_LIMIT
#define A733_TENSOR_PAGE 4096u
/* Heap/caller workspace only: never allocate this on the NuttX task stack. */
struct a733_tensor_plan_s
{
  struct a733_tensor_header_s header;
  struct a733_tensor_region_s region[A733_TENSOR_MAX_REGIONS];
  struct a733_tensor_desc_s input[A733_TENSOR_MAX_IO];
  struct a733_tensor_desc_s output[A733_TENSOR_MAX_IO];
  uint32_t pool_offset[A733_TENSOR_MAX_REGIONS];
  uint32_t tail_vip, tail_pool_offset, pool_bytes, descriptor_bytes;
};
typedef uint32_t (*a733_tensor_crc_t)(const uint8_t *, size_t, uint32_t);
int a733_tensor_header_check(const struct a733_tensor_header_s *h,
                             uint64_t file_size, uint32_t *descriptor_bytes);
int a733_tensor_plan_check(struct a733_tensor_plan_s *p, uint64_t file_size,
                           a733_tensor_crc_t crc);
/* Pure byte transport helpers, no hardware/casts/heap. Region/client pointers
 * come only from adapter-owned validated mappings. Buffers must be disjoint;
 * all requests/CRC preconditions checked before writes. */
int a733_tensor_input_check(const struct a733_tensor_plan_s *p,
                            const struct a733_tensor_run_s *run,
                            const void *const *input, a733_tensor_crc_t crc);
void a733_tensor_apply(const struct a733_tensor_plan_s *p, void *const *regions,
                       const void *const *input);
void a733_tensor_prefill(const struct a733_tensor_plan_s *p, void *const *regions);
int a733_tensor_output_check(const struct a733_tensor_plan_s *p,
                             struct a733_tensor_run_s *run,
                             void *const *regions, a733_tensor_crc_t crc);
void a733_tensor_commit(const struct a733_tensor_plan_s *p, void *const *regions,
                        void *const *output);
#endif
