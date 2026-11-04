/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "voice_local.hxx"
#include <cstddef>
namespace aipet {
using VoicePhraseSpeak = int (*)(void *, const char *, VoiceLocalCancel,
                                 void *);
using VoicePhraseFeed = int (*)(void *,VoiceTextNext,void *,VoiceLocalCancel,void *);
struct VoicePhrasePipe;
struct VoicePhraseMetrics {
  unsigned queued, spoken;
  long long first_phrase_us, first_synthesis_us;
};
/* Plain-text opt-in only: reject tool/JSON/control delimiters, bounded UTF-8
 * phrases/queue, worker synthesis overlaps synchronous LLM generation.
 * Spoken prefixes cannot be rolled back on a later LLM/TTS failure. */
int voice_phrase_open(VoicePhraseSpeak, void *, VoicePhrasePipe **);
int voice_phrase_open_feed(VoicePhraseFeed, void *, VoicePhrasePipe **);
int voice_phrase_accept(VoicePhrasePipe *, const char *, std::size_t);
int voice_phrase_finish(VoicePhrasePipe *, int upstream_error,
                        VoicePhraseMetrics *);
void voice_phrase_close(VoicePhrasePipe *);
} // namespace aipet
