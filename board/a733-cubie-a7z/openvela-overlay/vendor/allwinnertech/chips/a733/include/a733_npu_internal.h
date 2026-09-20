/****************************************************************************
 * vendor/allwinnertech/chips/a733/include/a733_npu_internal.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __VENDOR_ALLWINNERTECH_CHIPS_A733_INCLUDE_A733_NPU_INTERNAL_H
#define __VENDOR_ALLWINNERTECH_CHIPS_A733_INCLUDE_A733_NPU_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

struct a733_npu_hw_result_s
{
  uint32_t irq_before;
  uint32_t irq_after;
  uint32_t irq_value;
  uint32_t idle;
  uint32_t polls;
  uint32_t mmu_status;
  uint32_t mmu_exception;
  int recovery;
};

int a733_npu_hw_execute(uint32_t pd_entry, uint32_t command_address,
                        uint32_t timeout_ms, volatile bool *cancel,
                        bool restore,
                        struct a733_npu_hw_result_s *result);
int a733_npu_hw_execute_initialized(uint32_t pd_entry,
                                    uint32_t init_command_address,
                                    uint32_t command_address,
                                    uint32_t timeout_ms,
                                    volatile bool *cancel,
                                    struct a733_npu_hw_result_s *result);
int a733_npu_hw_reset(void);
uint32_t a733_npu_hw_cid(void);
int a733_npu_runtime_initialize(void);

#endif
