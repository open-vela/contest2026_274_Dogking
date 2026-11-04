/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "voice_acoustic.hxx"
#include "voice_local.hxx"
namespace aipet {
struct VoiceTtsEngine;
/* Immutable CPU frontend/acoustic session; serialized by engine mutex. No NPU
 * fd or thread survives the command task group. Optional scoped vocoder lease
 * rejects foreign process groups before touching their fd table. Explicit candidate,
 * not hardware qualification. Text <=4096 bytes, <=127 IDs per sentence,
 * <=10 seconds total; unsupported text fails, never canned/cloud fallback. */
/* Default noise=1 matches the upstream inference profile. Zero is available
 * only for deterministic CPU/oracle interface tests, not the default voice. */
int voice_tts_engine_open(const char *directory,VoiceTtsEngine **,float noise_scale=1.0f);
void voice_tts_engine_close(VoiceTtsEngine *);
/* Optional same-command NPU lease, serialized with synthesis. Begin/end in
 * the command owner; do not retain across task-group exit. */
int voice_tts_engine_session_begin(VoiceTtsEngine *);
int voice_tts_engine_session_end(VoiceTtsEngine *);
int voice_tts_engine_synthesize(VoiceTtsEngine *,const char *,VoiceLocalPcmSink,
    void *,long long deadline_us,VoiceLocalCancel,void *,VoiceAcousticStop=nullptr);
}
