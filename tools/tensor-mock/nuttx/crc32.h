/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TENSOR_TEST_CRC32_H
#define TENSOR_TEST_CRC32_H
#include <stddef.h>
#include <stdint.h>
#ifndef FAR
#define FAR
#endif
#ifdef __cplusplus
extern "C" {
#endif
uint32_t crc32part(const uint8_t *,size_t,uint32_t);
#ifdef __cplusplus
}
#endif
#endif
