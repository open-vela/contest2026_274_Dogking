/* SPDX-License-Identifier: Apache-2.0 */
/* Production session/configuration code, explicitly MOCK ASR/TTS/capture.
 * No NPU, CPU acoustic inference, files, microphones or playback. */
#include "voice_general_provider.hxx"
#include "voice_general_asr.hxx"
#include "voice_tts_engine.hxx"
#include "audio_capture_live.hxx"
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <pthread.h>
extern "C" void __lsan_disable();
extern "C" void __lsan_enable();
static unsigned opens=0,closes=0,resets=0,recordings=0,tts_opens=0,tts_closes=0,finishes=0,discards=0;
static int open_error=0,begin_error=0,capture_error=0;
static bool retained=false,empty=false;
#ifdef AIPET_TEST_RESIDENT
static unsigned idle_touches=0;
static unsigned monitored_turns=0;
static bool limited=false;
#endif
static aipet::VoiceLocalProvider installed{};
namespace aipet {
struct VoiceGeneralAsr {bool finished=false;};
struct VoiceTtsEngine {bool pinned=false;};
int voice_local_install(const VoiceLocalProvider *p){installed=p?*p:VoiceLocalProvider{};return 0;}
int voice_general_asr_open(const char *,long long,VoiceLocalCancel stop,void *cc,VoiceGeneralAsr **out) {
  *out=nullptr;if(open_error)return open_error;
  if(stop && stop(cc))return -ECANCELED;
  *out=new VoiceGeneralAsr;++opens;return 0;
}
int voice_general_asr_close(VoiceGeneralAsr *s){if(s){delete s;++closes;}return 0;}
int voice_general_asr_accept(VoiceGeneralAsr *,const unsigned char *,std::size_t){return 0;}
#ifdef AIPET_TEST_RESIDENT
int voice_general_asr_after_idle(VoiceGeneralAsr *s,long long){assert(!s->finished);++idle_touches;return 0;}
int audio_capture_dialogue(unsigned,unsigned,LiveSink sink,void *cookie,
    int (*cancel)(void *),void *cc,LiveVoiceState state,void *sc,LiveCaptureMetrics *m,uint64_t *noise) {
  assert(noise);
  assert(*noise==(monitored_turns==0||monitored_turns==2||monitored_turns==3?UINT64_MAX:1234));++monitored_turns;
  *noise=1234;
  *m={};m->limited=limited;m->consumer_retained=retained;
  if(retained)return -EOWNERDEAD;
  if(cancel && cancel(cc))return -ECANCELED;
  if(capture_error)return capture_error;
  if(state){state(sc,1);state(sc,2);}
  unsigned char pcm[320]{};
  int rc=sink(cookie,pcm,sizeof(pcm));return rc?rc:sink(cookie,pcm,sizeof(pcm));
}
#endif
int voice_general_asr_finish(VoiceGeneralAsr *s){++finishes;s->finished=true;return 0;}
int voice_general_asr_discard(VoiceGeneralAsr *s){assert(!s->finished);++discards;s->finished=true;return 0;}
int voice_general_asr_text(VoiceGeneralAsr *s,char *out,std::size_t n){assert(s->finished&&n>=32);std::strcpy(out,empty?"":"现在几点了");return 0;}
int voice_general_asr_stats(VoiceGeneralAsr *,VoiceAsrStreamStats *s){*s={160,1,24,true};return 0;}
int voice_general_asr_reset(VoiceGeneralAsr *s,long long,VoiceLocalCancel,void *) {
  assert(s->finished);s->finished=false;++resets;return 0;
}
int voice_tts_engine_open(const char *,VoiceTtsEngine **s,float){*s=new VoiceTtsEngine;return 0;}
void voice_tts_engine_close(VoiceTtsEngine *s){if(s){assert(!s->pinned);delete s;}}
int voice_tts_engine_session_begin(VoiceTtsEngine *s) {
  assert(!s->pinned);if(begin_error)return begin_error;s->pinned=true;++tts_opens;return 0;
}
int voice_tts_engine_session_end(VoiceTtsEngine *s){assert(s->pinned);s->pinned=false;++tts_closes;return 0;}
int voice_tts_engine_synthesize(VoiceTtsEngine *s,const char *,VoiceLocalPcmSink sink,void *cookie,
    long long,VoiceLocalCancel,void *,VoiceAcousticStop) {
  assert(s->pinned);unsigned char pcm[2]{};return sink(cookie,pcm,sizeof(pcm));
}
int audio_capture_live(unsigned,unsigned,unsigned,LiveSink,LiveSink pcm16,void *cookie,
    int (*)(void *),void *,LiveCaptureMetrics *m) {
  ++recordings;*m={};m->consumer_retained=retained;
  if(retained)return -EOWNERDEAD;
  if(capture_error)return capture_error;
  unsigned char pcm[320]{};return pcm16(cookie,pcm,sizeof(pcm));
}
}
static int sink(void *,const unsigned char *,std::size_t n){assert(n==2);return 0;}
int main() {
  using namespace aipet;
  assert(!voice_general_configure_full("/data/npu/asr","/data/npu/tts"));
  VoiceGeneralSession *s=nullptr;assert(!voice_general_session_open(&s,nullptr,nullptr));
  pthread_t foreign{};
  auto wrong_thread=[](void *cookie)->void * {
    auto *p=static_cast<VoiceGeneralSession *>(cookie);char ignored[64]{};
    assert(voice_general_tts_selected()==-EBUSY);
    assert(voice_general_session_listen(p,100,5,0,ignored,sizeof(ignored),nullptr,nullptr)==-EXDEV);
    assert(voice_general_session_close(p)==-EXDEV);
    voice_general_session_retain(p); // not the owner: must not poison this lease
    return nullptr;
  };
  assert(!pthread_create(&foreign,nullptr,wrong_thread,s));assert(!pthread_join(foreign,nullptr));
#ifdef AIPET_TEST_PRODUCT_PREPARE
  extern int product_prepare_regression();
  int prepared=product_prepare_regression();
  if(prepared) {
    std::printf("REPRO v1.0 resident -> production prepare = %d\n",prepared);
    assert(!voice_general_session_close(s));assert(!voice_general_configure("off"));
    return prepared==-EBUSY?86:87;
  }
#endif
  assert(voice_general_tts_selected()==1);
  assert(voice_general_configure("off")==-EBUSY);
  char text[64]="unchanged";
  for(unsigned i=0;i<3;i++) {
#ifdef AIPET_TEST_PRODUCT_PREPARE
    assert(!product_prepare_regression());
#endif
    assert(!voice_general_session_listen(s,100,5,0,text,sizeof(text),nullptr,nullptr));
    assert(!std::strcmp(text,"现在几点了"));
    assert(!installed.synthesize(installed.cookie,text,sink,nullptr,0,nullptr,nullptr));
  }
  assert(opens==1 && resets==2 && tts_opens==1);
  assert(!voice_general_session_close(s));assert(opens==closes&&tts_opens==tts_closes);
  open_error=-ENOMEM;assert(voice_general_session_open(&s,nullptr,nullptr)==-ENOMEM&&!s);open_error=0;
  begin_error=-EIO;assert(voice_general_session_open(&s,nullptr,nullptr)==-EIO&&!s);begin_error=0;
  assert(opens==closes&&voice_general_tts_selected()==1);
  assert(!voice_general_session_open(&s,nullptr,nullptr));
  capture_error=-EOVERFLOW;
  assert(voice_general_session_listen(s,100,5,0,text,sizeof(text),nullptr,nullptr)==-EOVERFLOW);
  unsigned before=recordings;
  assert(voice_general_session_listen(s,100,5,0,text,sizeof(text),nullptr,nullptr)==-EOVERFLOW&&recordings==before);
  assert(!voice_general_session_close(s));capture_error=0;
  assert(!voice_general_session_open(&s,nullptr,nullptr));empty=true;
  assert(!voice_general_session_listen(s,100,5,0,text,sizeof(text),nullptr,nullptr)&&!*text);
  empty=false;assert(!voice_general_session_listen(s,100,5,0,text,sizeof(text),nullptr,nullptr)&&*text);
  assert(!voice_general_session_close(s));
  assert(opens==closes&&tts_opens==tts_closes);
#ifdef AIPET_TEST_RESIDENT
  assert(!voice_general_session_open(&s,nullptr,nullptr));
  auto cancel=[](void *)->int{return 0;};
  unsigned before_touches=idle_touches,before_opens=opens,before_resets=resets,before_finishes=finishes,before_discards=discards;
  for(unsigned i=0;i<4;i++) {
    limited=i==1;empty=i==2;text[0]=0;
    int rc=voice_general_session_wait(s,5,400,text,sizeof(text),cancel,nullptr,nullptr,nullptr);
    assert(rc==(limited?-EMSGSIZE:0));assert(bool(*text)==(!limited&&!empty));
  }
  assert(opens==before_opens && resets==before_resets+3 && idle_touches==before_touches+4);
  assert(finishes==before_finishes+3 && discards==before_discards+1);
  puts("PASS v224: rejected length skips final decode, next turn recalibrates and resets without model reopen");
  empty=limited=false;assert(!voice_general_session_close(s));
  puts("PASS v214 actual session wait: one engine group, once-per-turn deadline refresh after arbitrary idle, no partial overlong turn, empty turn then resume");
#endif
  assert(!voice_general_configure("off"));
  puts("PASS: same-command ASR/vocoder reuse, per-turn reset, replacement exclusion, open/prepare/capture failure cleanup and empty transcript (host mocks)");
  assert(!voice_general_configure_full("/data/npu/asr","/data/npu/tts"));
  __lsan_disable();assert(!voice_general_session_open(&s,nullptr,nullptr));
  voice_general_session_retain(s);assert(voice_general_session_close(s)==-EOWNERDEAD);
  assert(voice_general_tts_selected()==-EOWNERDEAD);
  __lsan_enable();assert(voice_general_configure("off")==-EBUSY);
  puts("PASS: downstream retained worker pins ASR/TTS/config owner until reboot (intentional quarantine, not leak-free recovery)");
}
