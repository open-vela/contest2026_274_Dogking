/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "voice_local.hxx"
namespace aipet {
struct VoicePlayMetrics {
  long long first_pcm_us, first_pull_us;
  unsigned frames;
  long long start_mono_us=0, first_pull_mono_us=0;
  bool worker_retained=false;
  unsigned device_segments=0, rebuffers=0;
  long long rebuffer_wait_us=0;
};
/* Opt-in local synthesis streaming. 3s bounded queue, 100ms startup prebuffer,
 * nonblocking RAM reads while TX is live. Failure may leave an audible prefix;
 * abort/mute instead of pretending it was rolled back. No cloud or files. */
/* Temporary producer starvation ends a device segment by normal drain/stop,
 * then prebuffers with clocks quiet. No padding/replay; 60s total source PCM
 * and 120s overall deadline remain bounded. Segment gaps are not synthesized
 * speech and rebuffer_wait_us is NOT a physical audible-latency measurement.
 * v221fix1: stream text/feed both opt in to 60s; legacy buffered speak stays10s. */
/* -EOWNERDEAD + worker_retained requires retaining the cancel cookie until
 * reboot. The caller must not release borrowed context or resume I2S. */
int voice_play_stream(const char *, unsigned volume, VoiceLocalCancel, void *,
                      VoicePlayMetrics *);
// One PCM queue/resampler for a whole reply, bounded to 60s, 120s deadline.
int voice_play_feed(VoiceTextNext,void *,unsigned,VoiceLocalCancel,void *,VoicePlayMetrics *);
} // namespace aipet
