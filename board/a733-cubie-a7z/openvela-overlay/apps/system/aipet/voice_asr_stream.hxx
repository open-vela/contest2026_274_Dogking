/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "voice_asr_decode.h"
#include <cstddef>
namespace aipet {
/* Own one encoder's 35 states per session in the backend, not in a global
 * shared transcript. encode accepts logical [1,103,80], produces [24,512].
 * begin/end may retain validated weights but must reset utterance state.
 * No provider is installed by this core; native NBG layout remains a gate. */
struct VoiceAsrStreamOps {
  int (*begin)(void *);
  int (*encode)(void *, const float *features103x80, float *rows24x512);
  void (*end)(void *);
  aipet_asr_ops greedy;
};
struct VoiceAsrStream;
struct VoiceAsrStreamStats {
  unsigned samples, chunks, frames;
  bool finished;
  unsigned padded_frames=0; // waveform-zero frames actually appended at flush
};
int voice_asr_stream_open(const VoiceAsrStreamOps *, void *, VoiceAsrStream **);
int voice_asr_stream_accept(VoiceAsrStream *, const unsigned char *pcm16k,
                            std::size_t bytes);
int voice_asr_stream_finish(VoiceAsrStream *);
int voice_asr_stream_result(const VoiceAsrStream *, unsigned *tokens,
                            unsigned *positions, std::size_t capacity,
                            std::size_t *used);
int voice_asr_stream_stats(const VoiceAsrStream *, VoiceAsrStreamStats *);
void voice_asr_stream_close(VoiceAsrStream *);
} // namespace aipet
