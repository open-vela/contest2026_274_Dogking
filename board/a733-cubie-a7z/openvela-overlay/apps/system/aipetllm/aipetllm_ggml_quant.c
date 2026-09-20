/****************************************************************************
 * apps/system/aipetllm/aipetllm_ggml_quant.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "ggml-common.h"
#include "ggml-quants.h"
#include "quants.h"

int aipetllm_ggml_quant_checkpoint(void)
{
  float lhs[QK_K];
  float rhs[QK_K];
  float lhs_dequant[QK_K];
  float rhs_dequant[QK_K];
  block_q4_K lhs_q4;
  block_q8_K rhs_q8;
  float reference = 0.0f;
  float result = 0.0f;
  float error;
  int index;

  for (index = 0; index < QK_K; index++)
    {
      lhs[index] = (float)((index % 23) - 11) * 0.125f;
      rhs[index] = (float)((index % 17) - 8) * 0.0625f;
    }

  quantize_row_q4_K(lhs, &lhs_q4, QK_K);
  quantize_row_q8_K(rhs, &rhs_q8, QK_K);
  dequantize_row_q4_K(&lhs_q4, lhs_dequant, QK_K);
  dequantize_row_q8_K(&rhs_q8, rhs_dequant, QK_K);

  for (index = 0; index < QK_K; index++)
    {
      reference += lhs_dequant[index] * rhs_dequant[index];
    }

  ggml_vec_dot_q4_K_q8_K(QK_K, &result, 0, &lhs_q4, 0, &rhs_q8, 0, 1);
  error = fabsf(result - reference);
  printf("ggml-quant-check q4_k/q8_k elements=%d neon=%d "
         "reference=%.7g result=%.7g error=%.7g\n",
         QK_K,
#ifdef __ARM_NEON
         1,
#else
         0,
#endif
         (double)reference, (double)result, (double)error);

  return error <= 0.001f ? 0 : 1;
}
