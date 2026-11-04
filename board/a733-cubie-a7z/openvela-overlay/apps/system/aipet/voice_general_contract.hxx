/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <system/a733_tensor_client.h>
#include <cerrno>
#include <cstring>
namespace aipet {
/* Linux VIPLite native sizes from v183 real-audio/layout captures. These
 * are NOT the old 263-class KWS shapes, nbinfo allocation rounding or ONNX
 * int64 context. Every native interface is logical, contiguous float32. */
inline int voice_general_contract(const char *name,
                                  a733_tensor_client_contract &out) {
  static const uint32_t encoder[] = {
    32960,8,8,8,8,8,2048,2048,2048,2048,2048,294912,147456,73728,
    36864,147456,147456,73728,36864,18432,73728,147456,73728,36864,
    18432,73728,61440,61440,61440,61440,61440,61440,61440,61440,61440,61440};
  static_assert(sizeof(encoder)/sizeof(*encoder)==36, "35 recurrent states");
  out = {};
  if (!std::strcmp(name,"asr-encoder")) {
    out.inputs=out.outputs=36;
    std::memcpy(out.input_bytes,encoder,sizeof(encoder));
    std::memcpy(out.output_bytes,encoder,sizeof(encoder));
    out.output_bytes[0]=49152;
  } else if (!std::strcmp(name,"asr-decoder")) {
    out.inputs=out.outputs=1;out.input_bytes[0]=8;out.output_bytes[0]=2048;
  } else if (!std::strcmp(name,"asr-joiner")) {
    out.inputs=2;out.outputs=1;out.input_bytes[0]=out.input_bytes[1]=2048;
    out.output_bytes[0]=25016;
  } else return -EINVAL;
  return 0;
}
}
