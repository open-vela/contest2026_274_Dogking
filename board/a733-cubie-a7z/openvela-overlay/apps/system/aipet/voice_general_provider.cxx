/* SPDX-License-Identifier: Apache-2.0 */
#include "voice_general_provider.hxx"
#include "voice_general_asr.hxx"
#include "voice_load_log.hxx"
#include "audio_capture_live.hxx"
#ifdef __NuttX__
#include <nuttx/config.h>
#endif
#ifdef CONFIG_A733_CPU_ACOUSTIC
#include "voice_tts_engine.hxx"
#include <signal.h>
#endif
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <new>
#include <pthread.h>
#include <unistd.h>
namespace aipet {
namespace {
struct Configuration {
  char directory[96]{};
#ifdef CONFIG_A733_CPU_ACOUSTIC
  VoiceTtsEngine *tts=nullptr;
  ~Configuration(){voice_tts_engine_close(tts);}
#endif
};
Configuration *current=nullptr;
pthread_mutex_t configuration_lock=PTHREAD_MUTEX_INITIALIZER;
/* The long configuration lease must remain held across ASR -> reply -> next
 * listen. Its OWNER may query immutable readiness without recursively taking
 * that mutex. Other threads (including the same process) still get EBUSY;
 * this short metadata lock never releases/borrows the ASR or vocoder owner. */
struct ConfigurationLease {
  pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
  bool active=false,tts=false,retained=false;
  pid_t group=0;pthread_t thread{};
  void publish(bool has_tts) {
    pthread_mutex_lock(&lock);group=getpid();thread=pthread_self();
    tts=has_tts;retained=false;active=true;pthread_mutex_unlock(&lock);
  }
  int selected() {
    pthread_mutex_lock(&lock);int rc=-EBUSY;
    if(active && group==getpid() && pthread_equal(thread,pthread_self()))
      rc=retained?-EOWNERDEAD:tts?1:0;
    pthread_mutex_unlock(&lock);return rc;
  }
  void mark_retained() {
    pthread_mutex_lock(&lock);retained=true;pthread_mutex_unlock(&lock);
  }
  void clear() {
    pthread_mutex_lock(&lock);active=false;tts=false;group=0;
    pthread_mutex_unlock(&lock);
  }
} configuration_lease;
long long now() {
  timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);
  return static_cast<long long>(t.tv_sec)*1000000+t.tv_nsec/1000;
}
int recognize(void *cookie,const unsigned char *pcm,std::size_t bytes,
    char *text,std::size_t capacity,long long deadline,VoiceLocalCancel cancel,void *cc) {
  if(!pcm || !bytes || bytes%2 || bytes>256000 || !text || !capacity || capacity>8192)return -EINVAL;
  auto *cfg=static_cast<Configuration *>(cookie);
  std::unique_ptr<char[]> result(new(std::nothrow) char[capacity]);
  if(!result)return -ENOMEM;
  VoiceGeneralAsr *s=nullptr;
  int rc=voice_general_asr_open(cfg->directory,deadline,cancel,cc,&s);
  if(!rc)rc=voice_general_asr_accept(s,pcm,bytes);
  if(!rc)rc=voice_general_asr_finish(s);
  if(!rc)rc=voice_general_asr_text(s,result.get(),capacity);
  int close=voice_general_asr_close(s);if(!rc)rc=close;
  if(!rc)std::memcpy(text,result.get(),std::strlen(result.get())+1);
  return rc;
}
int pcm16(void *cookie,const unsigned char *pcm,std::size_t bytes) {
  return voice_general_asr_accept(static_cast<VoiceGeneralAsr *>(cookie),pcm,bytes);
}
bool allowed(const char *dir) {
  return dir && std::strlen(dir)<=95 &&
      (!std::strncmp(dir,"/data/npu/",10) || !std::strncmp(dir,"/ufs/npu/",9));
}
#ifdef CONFIG_A733_CPU_ACOUSTIC
int stop_cpu() {
  // Same blocked-SIGINT policy as the synchronous audio owner. No caller
  // stack/cookie is retained by ORT's deadline monitor. Directed-thread
  // signal responsiveness still requires target testing; caller rechecks.
  sigset_t pending;
  return sigpending(&pending)==0 && sigismember(&pending,SIGINT)==1 ? -ECANCELED:0;
}
int synthesize(void *cookie,const char *text,VoiceLocalPcmSink sink,void *sink_cookie,
    long long deadline,VoiceLocalCancel cancel,void *cc) {
  auto *cfg=static_cast<Configuration *>(cookie);
  return voice_tts_engine_synthesize(cfg->tts,text,sink,sink_cookie,deadline,cancel,cc,stop_cpu);
}
#endif
}
int voice_general_configure(const char *directory) {
  if(!directory)return -EINVAL;
  if(pthread_mutex_trylock(&configuration_lock))return -EBUSY;
  int rc=0;Configuration *next=nullptr;
  if(std::strcmp(directory,"off")) {
    if(!allowed(directory))rc=-EINVAL;
    else {
      next=new(std::nothrow) Configuration;
      if(!next)rc=-ENOMEM;
      else std::strcpy(next->directory,directory);
    }
  }
  if(!rc) {
    VoiceLocalProvider p{"zipformer-general-v184-candidate",2,0,0,next,recognize,nullptr};
    rc=voice_local_install(next?&p:nullptr);
    if(!rc){delete current;current=next;next=nullptr;}
  }
  bool configured=current!=nullptr;
  delete next;pthread_mutex_unlock(&configuration_lock);
  if(!rc)std::puts(configured ? "general-asr: candidate configured (RAM only); TTS unavailable; target acceptance required"
                          : "general-asr: uninstalled; no capture/inference running");
  return rc;
}
int voice_general_configure_full(const char *asr,const char *tts) {
#ifndef CONFIG_A733_CPU_ACOUSTIC
  (void)asr;(void)tts;return -ENOTSUP;
#else
  if(!allowed(asr) || !allowed(tts))return -EINVAL;
  if(pthread_mutex_trylock(&configuration_lock))return -EBUSY;
  auto *next=new(std::nothrow) Configuration;
  int rc=next?0:-ENOMEM;
  VoiceLoadLog load("general-provider-configure",tts);
  std::printf("voice-load-v215: requested-ASR=%s requested-TTS=%s\n",asr,tts);
  std::fflush(stdout);
  if(!rc){std::strcpy(next->directory,asr);rc=voice_tts_engine_open(tts,&next->tts);}
  if(!rc) {
    VoiceLocalProvider p{"general-voice-v185-candidate",2,1,2,next,recognize,synthesize};
    rc=voice_local_install(&p);
    if(!rc){delete current;current=next;next=nullptr;}
  }
  delete next;pthread_mutex_unlock(&configuration_lock);
  if(!rc)std::puts("general-voice: RAM-only candidate installed; ASR=NPU acoustic=CPU vocoder=NPU; no auto capture/cloud fallback; TARGET ACCEPTANCE REQUIRED");
  return load.finish(rc);
#endif
}
int voice_general_tts_selected() {
  if(pthread_mutex_trylock(&configuration_lock))return configuration_lease.selected();
  int selected=0;
#ifdef CONFIG_A733_CPU_ACOUSTIC
  selected=current && current->tts;
#endif
  pthread_mutex_unlock(&configuration_lock);return selected;
}
struct VoiceGeneralSession {
  uint64_t idle_noise=UINT64_MAX;
  VoiceGeneralAsr *asr=nullptr;
  pid_t group=getpid();
  pthread_t thread=pthread_self();
#ifdef CONFIG_A733_CPU_ACOUSTIC
  VoiceTtsEngine *tts=nullptr;
#endif
  bool used=false,retained=false;
  int error=0;
};
int voice_general_session_open(VoiceGeneralSession **out,VoiceLocalCancel cancel,void *cookie) {
  if(!out)return -EINVAL;
  *out=nullptr;
  if(pthread_mutex_trylock(&configuration_lock))return -EBUSY;
  if(!current){pthread_mutex_unlock(&configuration_lock);return -ENODEV;}
  auto *s=new(std::nothrow) VoiceGeneralSession;
  if(!s){pthread_mutex_unlock(&configuration_lock);return -ENOMEM;}
  const long long at=now();
  int rc=voice_general_asr_open(current->directory,at+120000000LL,cancel,cookie,&s->asr);
  const long long asr_ready=now();
#ifdef CONFIG_A733_CPU_ACOUSTIC
  if(!rc && current->tts){rc=voice_tts_engine_session_begin(current->tts);if(!rc)s->tts=current->tts;}
#endif
  std::printf("voice-prepare-v213: asr-us=%lld vocoder-us=%lld status=%d; MIC still OFF\n",
              asr_ready-at,now()-asr_ready,rc);
  if(rc){voice_general_asr_close(s->asr);delete s;pthread_mutex_unlock(&configuration_lock);return rc;}
  std::printf("voice-session-v190: prepared ASR once in %lld us; same-command RAM reuse, per-turn states cleared\n",now()-at);
  bool has_tts=false;
#ifdef CONFIG_A733_CPU_ACOUSTIC
  has_tts=s->tts!=nullptr;
#endif
  configuration_lease.publish(has_tts);
  *out=s;return 0;
}
int voice_general_session_close(VoiceGeneralSession *s) {
  if(!s)return 0;
  if(s->group!=getpid() || !pthread_equal(s->thread,pthread_self()))return -EXDEV;
  if(s->retained)return -EOWNERDEAD; // owner + configuration pinned until reboot
  int rc=voice_general_asr_close(s->asr);
#ifdef CONFIG_A733_CPU_ACOUSTIC
  if(s->tts){int closed=voice_tts_engine_session_end(s->tts);if(!rc)rc=closed;}
#endif
  configuration_lease.clear();delete s;
  pthread_mutex_unlock(&configuration_lock);return rc;
}
void voice_general_session_retain(VoiceGeneralSession *s) {
  if(s && s->group==getpid() && pthread_equal(s->thread,pthread_self())){configuration_lease.mark_retained();s->retained=true;s->error=-EOWNERDEAD;}
}
int voice_general_session_listen(VoiceGeneralSession *owner,unsigned ms,unsigned gain,unsigned endpoint,char *text,
    std::size_t capacity,VoiceLocalCancel cancel,void *cookie) {
  if(!owner || !text || !capacity || capacity>8192 || ms<100 || ms>8000 ||
     !gain || gain>16 || (endpoint && (endpoint<100 || endpoint>5000)))return -EINVAL;
  if(owner->group!=getpid() || !pthread_equal(owner->thread,pthread_self()))return -EXDEV;
  if(owner->error)return owner->error;
  std::unique_ptr<char[]> result(new(std::nothrow) char[capacity]);
  if(!result)return -ENOMEM;
  auto *s=owner->asr;LiveCaptureMetrics metrics{};
  int rc=owner->used?voice_general_asr_reset(s,now()+120000000LL,cancel,cookie):0;
  if(!rc) {
    std::puts("general-asr: MIC ON; live 48k->16k FIR->NPU; capture-side energy endpoint; no actions");
    rc=audio_capture_live(ms,gain,endpoint,nullptr,pcm16,s,cancel,cookie,&metrics);
    std::puts(metrics.consumer_retained ? "general-asr: MIC OFF; consumer ownership uncertain" :
              "general-asr: MIC OFF; capture/consumer stopped before final decode or playback");
  }
  if(metrics.consumer_retained) {
    // The consumer may still be inside an ASR callback. Closing s here would
    // free its tensors/fds while they are in use. CLI cancellation has static
    // lifetime and a null cookie. Other callers must obey the retained-owner
    // contract and keep any external cancel cookie alive until reboot.
    std::puts("general-asr: consumer join failed; ASR owner retained; no transcript; REBOOT REQUIRED");
    configuration_lease.mark_retained();owner->retained=true;return owner->error=-EOWNERDEAD;
  }
  if(!rc)rc=voice_general_asr_finish(s);
  if(!rc)rc=voice_general_asr_text(s,result.get(),capacity);
  VoiceAsrStreamStats stats{};if(s)voice_general_asr_stats(s,&stats);
  if(!rc)owner->used=true;
  else owner->error=rc;
  if(!rc)std::memcpy(text,result.get(),std::strlen(result.get())+1);
  std::printf("general-asr: result=%d samples16=%u chunks=%u queue-peak=%u ended=%u; native NPU candidate, not E2E acceptance\n",
    rc,metrics.samples16,stats.chunks,metrics.queue_peak,metrics.ended);
  std::printf("voice-stage: mic-stopped-mono-us=%lld transcript-ready-mono-us=%lld; mic stop is NOT true end-of-speech (energy endpoint is diagnostic)\n",
      metrics.capture_stopped_us,rc?0:now());
  return rc;
}
int voice_general_live(unsigned ms,unsigned gain,unsigned endpoint,char *text,
    std::size_t capacity,VoiceLocalCancel cancel,void *cookie) {
  if(!text || !capacity || capacity>8192 || ms<100 || ms>8000 ||
     !gain || gain>16 || (endpoint && (endpoint<100 || endpoint>5000)))return -EINVAL;
  VoiceGeneralSession *s=nullptr;
  int rc=voice_general_session_open(&s,cancel,cookie);
  if(!rc)rc=voice_general_session_listen(s,ms,gain,endpoint,text,capacity,cancel,cookie);
  int closed=voice_general_session_close(s);return rc?rc:closed;
}

int voice_general_session_wait(VoiceGeneralSession *owner,unsigned gain,unsigned floor,
    char *text,std::size_t capacity,VoiceLocalCancel cancel,void *cookie,
    LiveVoiceState state,void *state_cookie) {
#if !defined(__NuttX__) && !defined(AIPET_TEST_RESIDENT)
  (void)owner;(void)gain;(void)floor;(void)text;(void)capacity;(void)cancel;
  (void)cookie;(void)state;(void)state_cookie;return -ENOTSUP;
#else
  if(!owner || !text || !capacity || capacity>8192 || !cancel)return -EINVAL;
  text[0]=0;
  if(owner->group!=getpid() || !pthread_equal(owner->thread,pthread_self()))return -EXDEV;
  if(owner->error)return owner->error;
  struct Feed {VoiceGeneralAsr *asr;bool started=false;};
  std::unique_ptr<Feed> feed(new(std::nothrow) Feed{owner->asr});
  if(!feed)return -ENOMEM;
  int rc=owner->used?voice_general_asr_reset(owner->asr,now()+120000000LL,cancel,cookie):0;
  auto accept=[](void *p,const unsigned char *pcm,std::size_t n)->int {
    auto *f=static_cast<Feed *>(p);int r=0;
    if(!f->started){r=voice_general_asr_after_idle(f->asr,now()+120000000LL);if(!r)f->started=true;}
    return r?r:voice_general_asr_accept(f->asr,pcm,n);
  };
  LiveCaptureMetrics metrics{};
  if(!rc)rc=audio_capture_dialogue(gain,floor,accept,feed.get(),cancel,cookie,state,state_cookie,&metrics,&owner->idle_noise);
  if(metrics.consumer_retained){feed.release();configuration_lease.mark_retained();owner->retained=true;return owner->error=-EOWNERDEAD;}
  // No final NPU work for audio already rejected by the length policy. The
  // capture call has stopped RX AND joined the synchronous consumer. A failed
  // capture/consumer above must NEVER take this healthy-discard path.
  if(!rc && metrics.limited) {
    VoiceLoadLog discard("resident-ASR-discard-no-final-NPU");
    rc=voice_general_asr_discard(owner->asr);discard.finish(rc);
    if(!rc){owner->used=true;owner->idle_noise=UINT64_MAX;}
    else owner->error=rc;
    std::printf("voice-resident-v224: limited=1 capture-and-consumer-joined; no final decode/text/LLM/action; discard-status=%d; next turn recalibrates\n",rc);
    std::fflush(stdout);return rc?rc:-EMSGSIZE;
  }
  if(!rc){VoiceLoadLog finish("resident-ASR-finalize");rc=voice_general_asr_finish(owner->asr);finish.finish(rc);}
  if(!rc){owner->used=true;if(!metrics.limited)rc=voice_general_asr_text(owner->asr,text,capacity);}
  if(!rc && !metrics.limited && !*text)owner->idle_noise=UINT64_MAX; // recalibrate after a false trigger
  if(rc)owner->error=rc;
  std::printf("voice-resident-v214: asr=%d accepted16=%u limited=%u; models retained by SAME live task\n",
    rc,metrics.samples16,metrics.limited);
  std::printf("voice-stage-v214: capture-stop-mono-us=%lld consumer-done-mono-us=%lld asr-ready-mono-us=%lld tail-us=%lld status=%d\n",
    metrics.capture_stopped_us,metrics.consumer_finished_us,now(),
    metrics.capture_stopped_us?now()-metrics.capture_stopped_us:0,rc);
  return rc?rc:metrics.limited?-EMSGSIZE:0;
#endif
}
}
