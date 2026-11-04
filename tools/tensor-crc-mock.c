/* SPDX-License-Identifier: Apache-2.0 */
/* Host application seam uses the ACTUAL v160 helper; official SDK CRC remains
 * linked independently for cross-checking, never used as a fake NPU backend. */
#include <system/a733_tensor_client.h>
#include <nuttx/crc32.h>
#include "../board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733/a733_tensor_crc.inc"
uint32_t a733_tensor_client_crc32(const void *p,size_t n,uint32_t seed)
{ return a733_tensor_crc32part(p,n,seed); }
