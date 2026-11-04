/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <cstddef>
namespace aipet {
using VoiceLocalCancel = int (*)(void *);
using VoiceLocalPcmSink = int (*)(void *,const unsigned char *,std::size_t);
// Nonblocking producer: 1=complete clause, 0=EOF, -EAGAIN=not yet,
// other negative=error. Never wait inside this callback: caller must cancel.
using VoiceTextNext = int (*)(void *,char *,std::size_t);
/* A target adapter must install real, qualified local engines. No provider is
 * installed by default. No host Python, Linux .so, canned WAV, KWS, or cloud
 * may be advertised as the general target ASR / arbitrary-text TTS provider.
 * TTS emits mono S16LE 22050Hz chunks <=51200 samples. Buffered callers
 * enforce10s; explicitly streaming callers allow60s, both with120s deadline.
 * Callbacks get an absolute CLOCK_MONOTONIC deadline and must bound their own
 * engine calls. All pointers/cookies must survive until uninstall succeeds.
 */
struct VoiceLocalProvider {
  const char *name;
  unsigned asr_backend; /* 0 unavailable, 1 CPU, 2 NPU */
  unsigned acoustic_backend;
  unsigned vocoder_backend;
  void *cookie;
  int (*recognize)(void *,const unsigned char *,std::size_t,char *,std::size_t,
                   long long deadline_us,VoiceLocalCancel,void *);
  int (*synthesize)(void *,const char *,VoiceLocalPcmSink,void *,
                    long long deadline_us,VoiceLocalCancel,void *);
};
int voice_local_install(const VoiceLocalProvider *); /* null = uninstall; busy fails */
unsigned voice_local_capabilities(); /* bit 0 ASR, bit 1 TTS; no inference */
void voice_local_status();
int voice_local_recognize(const unsigned char *,std::size_t,char *,std::size_t,
                           VoiceLocalCancel,void *);
int voice_local_speak(const char *,VoiceLocalPcmSink,void *,VoiceLocalCancel,void *);
// Opt-in RAM streaming only: 60s total; legacy speak remains <=10s.
int voice_local_speak_stream(const char *,VoiceLocalPcmSink,void *,VoiceLocalCancel,void *);
}
