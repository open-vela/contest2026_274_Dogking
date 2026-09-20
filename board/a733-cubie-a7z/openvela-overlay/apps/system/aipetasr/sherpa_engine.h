/* SPDX-License-Identifier: Apache-2.0 */
#ifndef LOCAL_ASR_SHERPA_ENGINE_H
#define LOCAL_ASR_SHERPA_ENGINE_H
#include "local_asr.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Caller retains paths until the stream is destroyed. All files must match
 * the same 16kHz streaming transducer model; not arbitrary ncnn assets. */
struct local_asr_sherpa_config
{
  const char *encoder_param, *encoder_bin;
  const char *decoder_param, *decoder_bin;
  const char *joiner_param, *joiner_bin;
  const char *tokens;
  unsigned threads;
};
extern const struct local_asr_engine local_asr_sherpa_engine;
#ifdef __cplusplus
}
#endif
#endif
