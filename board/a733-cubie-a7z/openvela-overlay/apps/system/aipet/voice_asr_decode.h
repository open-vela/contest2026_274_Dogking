/* SPDX-License-Identifier: Apache-2.0 */
#ifndef AIPET_VOICE_ASR_DECODE_H
#define AIPET_VOICE_ASR_DECODE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* General bilingual Zipformer profile, NOT the 263-token KWS profile.
 * The backend owns encoder state and native layout conversion. This module
 * consumes contiguous logical [time,512] FP32 rows, one symbol per frame,
 * matching the Model Zoo greedy search. One caller owns each session.
 * Backend calls must themselves have bounded waits. Cancellation here cannot
 * interrupt a stuck callback, reset hardware, or license unqualified NBGs.
 */
#define AIPET_ASR_DIM 512u
#define AIPET_ASR_VOCAB 6254u
#define AIPET_ASR_MAX_TOKENS 1024u
#define AIPET_ASR_MAX_FRAMES 1024u
struct aipet_asr_ops {
  int (*decoder)(void *, const int64_t context[2], float *out512);
  int (*joiner)(void *, const float *encoder512, const float *decoder512,
                float *logits6254);
  int (*cancelled)(void *); /* 0, or negative errno; required */
};
struct aipet_asr_decoder;
int aipet_asr_decoder_open(const struct aipet_asr_ops *, void *,
                          struct aipet_asr_decoder **);
int aipet_asr_decoder_accept(struct aipet_asr_decoder *, const float *, size_t frames);
int aipet_asr_decoder_finish(struct aipet_asr_decoder *);
/* Publishes only a successfully finished utterance; never a partial command. */
int aipet_asr_decoder_result(const struct aipet_asr_decoder *, uint32_t *tokens,
                            uint32_t *frame_positions, size_t capacity, size_t *used);
void aipet_asr_decoder_close(struct aipet_asr_decoder *);
#ifdef __cplusplus
}
#endif
#endif
