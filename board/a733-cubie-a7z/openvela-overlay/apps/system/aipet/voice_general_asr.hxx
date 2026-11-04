/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "voice_asr_stream.hxx"
#include "voice_local.hxx"
namespace aipet {
struct VoiceGeneralAsr;
/* Owns three tensor clients, their native buffers and one utterance's 35
 * recurrent states. Create/use/close in the same live NuttX task group.
 * Models and tokens are local trusted files; no upload or fallback. */
int voice_general_asr_open(const char *directory, long long deadline_us,
                           VoiceLocalCancel, void *, VoiceGeneralAsr **);
int voice_general_asr_accept(VoiceGeneralAsr *, const unsigned char *pcm16k,
                             std::size_t bytes);
int voice_general_asr_finish(VoiceGeneralAsr *);
int voice_general_asr_text(VoiceGeneralAsr *, char *, std::size_t);
int voice_general_asr_stats(VoiceGeneralAsr *, VoiceAsrStreamStats *);
/* Reuse immutable models/fds ONLY within the same live command task group.
 * Requires a successfully finished utterance; clears ALL recurrent/decoder
 * states and timing. Failed/unfinished sessions are never silently retried. */
int voice_general_asr_reset(VoiceGeneralAsr *,long long,VoiceLocalCancel,void *);
/* ONLY after capture stopped AND its synchronous ASR consumer joined with
 * success. Abandon an intentionally rejected utterance WITHOUT final NPU
 * decode. No hardware reset/cancel/retry. Pins the same three clients; reset
 * must explicitly clear ALL stream/recurrent state before the next accept.
 * Never call on an in-flight, failed or uncertain-owner session. */
int voice_general_asr_discard(VoiceGeneralAsr *);
/* Refresh an otherwise pristine utterance after explicit unbounded idle.
 * Called once by the first PCM consumer before accept, never during inference. */
int voice_general_asr_after_idle(VoiceGeneralAsr *,long long);
int voice_general_asr_close(VoiceGeneralAsr *);
}
