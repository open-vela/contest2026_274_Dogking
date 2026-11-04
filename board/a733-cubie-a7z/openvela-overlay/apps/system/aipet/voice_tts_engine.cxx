/* SPDX-License-Identifier: Apache-2.0 */
#include "voice_tts_engine.hxx"
#include "voice_load_log.hxx"
#include "voice_tts_frontend.hxx"
#include "voice_compute.hxx"
#include <system/a733_tensor_client.h>
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <memory>
#include <new>
#include <cstring>
#include <cmath>
#include <pthread.h>
#include <unistd.h>
namespace aipet {
struct VoiceTtsEngine {
  VoiceTtsFrontend *frontend=nullptr;
  VoiceAcoustic *acoustic=nullptr;
  char vocoder[128]{};
  float noise=1.0f;
  std::unique_ptr<float[]> mel,wave;
  std::unique_ptr<unsigned char[]> pcm;
  a733_tensor_client *pinned=nullptr;
  pid_t pinned_group=0;
  pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
  ~VoiceTtsEngine() {
    auto wipe=[](void *data,std::size_t n) {
      if(!data)return;
      volatile unsigned char *p=static_cast<unsigned char *>(data);
      while(n--)*p++=0;
    };
    wipe(mel.get(),64000);wipe(wave.get(),204800);wipe(pcm.get(),102400);
  }
};
namespace {
struct EngineLease {
  VoiceTtsEngine *engine;
  ~EngineLease(){pthread_mutex_unlock(&engine->lock);}
};
long long now() {
  timespec t{};if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;
  return static_cast<long long>(t.tv_sec)*1000000+t.tv_nsec/1000;
}
int path(char *out,std::size_t cap,const char *dir,const char *name) {
  int n=std::snprintf(out,cap,"%s/%s",dir,name);
  return n<0 || static_cast<std::size_t>(n)>=cap ? -ENAMETOOLONG:0;
}
struct Operation {
  a733_tensor_client *client=nullptr;
  bool borrowed=false;
  long long deadline;
  VoiceLocalCancel cancel;
  void *cookie;
  int poll() {
    int rc=cancel?cancel(cookie):0;
    if(rc)return rc<0?rc:-ECANCELED;
    long long t=now();return t<0?-EIO:t>=deadline?-ETIMEDOUT:0;
  }
  ~Operation(){if(client && !borrowed)a733_tensor_client_close(client);}
};
}
int voice_tts_engine_open(const char *directory,VoiceTtsEngine **out,float noise) {
  if(!out)return -EINVAL;
  *out=nullptr;if(!directory || !*directory || std::strlen(directory)>95 ||
                 !std::isfinite(noise) || noise<0 || noise>1)return -EINVAL;
  std::unique_ptr<VoiceTtsEngine> s(new(std::nothrow) VoiceTtsEngine);
  if(!s)return -ENOMEM;
  s->noise=noise;
  s->mel.reset(new(std::nothrow) float[16000]);
  s->wave.reset(new(std::nothrow) float[51200]);
  s->pcm.reset(new(std::nothrow) unsigned char[102400]);
  char tokens[128],lexicon[128],model[128];
  int rc=(!s->mel || !s->wave || !s->pcm)?-ENOMEM:0;
  if(!rc)rc=path(tokens,sizeof(tokens),directory,"tokens.txt");
  if(!rc)rc=path(lexicon,sizeof(lexicon),directory,"lexicon.txt");
  if(!rc)rc=path(model,sizeof(model),directory,"model-steps-3.basic.ort");
  if(!rc)rc=path(s->vocoder,sizeof(s->vocoder),directory,"vocoder.a7tv");
  if(!rc) {
    VoiceLoadLog load("tts-tokens-lexicon",directory);
    rc=voice_tts_frontend_open(tokens,lexicon,&s->frontend);load.finish(rc);
  }
  if(!rc)rc=voice_acoustic_open(model,&s->acoustic);
  if(rc){voice_tts_engine_close(s.release());return rc;}
  *out=s.release();return 0;
}
void voice_tts_engine_close(VoiceTtsEngine *s) {
  if(!s)return;
  // A foreign group must never close/use a scoped fd or destroy its engine.
  if(voice_tts_engine_session_end(s))return;
  if(pthread_mutex_destroy(&s->lock))return;
  voice_acoustic_close(s->acoustic);voice_tts_frontend_close(s->frontend);delete s;
}
int voice_tts_engine_session_begin(VoiceTtsEngine *s) {
  if(!s)return -EINVAL;
  if(pthread_mutex_trylock(&s->lock))return -EBUSY;
  EngineLease lease{s};
  if(s->pinned)return -EBUSY;
  a733_tensor_client_contract c{};c.inputs=c.outputs=1;
  c.input_bytes[0]=64000;c.output_bytes[0]=204800;
  VoiceLoadLog query("vocoder-query",s->vocoder);
  int rc=a733_tensor_client_open(s->vocoder,&c,&s->pinned);
  query.finish(rc);
  if(!rc) {
    VoiceLoadLog load("vocoder-native-prepare",s->vocoder);
    rc=a733_tensor_client_prepare(s->pinned);load.finish(rc);
  }
  if(rc && s->pinned){int closed=a733_tensor_client_close(s->pinned);s->pinned=nullptr;if(closed)rc=closed;}
  if(!rc)s->pinned_group=getpid();
  return rc;
}
int voice_tts_engine_session_end(VoiceTtsEngine *s) {
  if(!s)return -EINVAL;
  // Owner calls this after downstream joins. A foreign command can only
  // briefly enter, see EXDEV below and leave; never borrow this group's fd.
  int locked=pthread_mutex_lock(&s->lock);if(locked)return -locked;
  EngineLease lease{s};
  if(!s->pinned)return 0;
  if(s->pinned_group!=getpid())return -EPERM;
  int rc=a733_tensor_client_close(s->pinned);s->pinned=nullptr;s->pinned_group=0;return rc;
}
int voice_tts_engine_synthesize(VoiceTtsEngine *s,const char *text,
    VoiceLocalPcmSink sink,void *sink_cookie,long long deadline,
    VoiceLocalCancel cancel,void *cancel_cookie,VoiceAcousticStop stop) {
  if(!s || !text || !sink)return -EINVAL;
  if(pthread_mutex_trylock(&s->lock))return -EBUSY;
  EngineLease lease{s};
  if(s->pinned && s->pinned_group!=getpid())return -EXDEV;
  const long long start=now();if(start<0)return -EIO;
  if(deadline<=start)return -ETIMEDOUT;
  if(deadline-start>120000000LL)return -EINVAL;
  Operation op{s->pinned,s->pinned!=nullptr,deadline,cancel,cancel_cookie};
  int rc=op.poll();std::vector<std::vector<int64_t>> sentences;
  if(!rc)rc=voice_tts_frontend_text(s->frontend,text,sentences);
  if(!rc && sentences.empty())rc=-ENODATA;
  // Validate the ENTIRE text before inference/output. Never skip an unknown
  // sentence, omit IDs, truncate a model input or silently switch engines.
  for(const auto &ids:sentences) {
    if(ids.empty() || ids.size()>127){rc=-EMSGSIZE;break;}
  }
  // Engine mutex serializes these bounded buffers; sinks synchronously copy
  // their input before return. Fully overwritten by the original algorithms.
  auto &mel=s->mel;auto &wave=s->wave;auto &pcm=s->pcm;
  a733_tensor_client_contract contract{};
  contract.inputs=contract.outputs=1;
  contract.input_bytes[0]=64000;contract.output_bytes[0]=204800;
  if(!rc)rc=op.poll();
  if(!rc && !op.borrowed)rc=a733_tensor_client_open(s->vocoder,&contract,&op.client);
  if(!rc && !op.borrowed)rc=a733_tensor_client_prepare(op.client);
  unsigned samples=0,packets=0,pcm_nonzero=0,pcm_peak=0;long long cpu_us=0,npu_us=0,first_us=-1;
  for(const auto &ids:sentences) {
    if(rc)break;
    rc=op.poll();if(rc)break;
    VoiceAcousticMel features{};long long at=now();
    rc=voice_acoustic_run(s->acoustic,ids.data(),ids.size(),s->noise,1.0f,
                           &features,deadline,stop);
    cpu_us+=now()-at;
    if(!rc)rc=op.poll();
    // Streaming can carry up to 60s; callers of the legacy buffered API
    // still enforce their own 10s cap. Per-run mel/float bounds are unchanged.
    if(!rc && features.frames*256u>1323000u-samples)rc=-EMSGSIZE;
    // Context overlap around vocoder windows. Crop only the context output,
    // not speech: each real mel frame is emitted exactly once in order.
    // Short (<=200) golden cases retain their original complete input.
    for(unsigned first=0;!rc && first<features.frames;) {
      unsigned kept=0,skip=0;
      rc=voice_tts_vocoder_window(features.data,features.frames,first,mel.get(),16000,&skip,&kept);
      if(!rc)rc=op.poll();
      a733_tensor_client_input input{mel.get(),64000};
      a733_tensor_client_output output{wave.get(),204800};
      long long remaining=(deadline-now()+999)/1000;
      if(!rc && remaining<=0)rc=-ETIMEDOUT;
      if(!rc) {
        at=now();
        VoiceCompute compute(6);rc=compute.begin();
        if(!rc)rc=a733_tensor_client_execute(op.client,&input,&output,
             static_cast<unsigned>(remaining>5000?5000:remaining),0);
        rc=compute.finish(rc);
        npu_us+=now()-at;
      }
      // Check all 51200 returned floats, including the padded tail.
      if(!rc)rc=voice_tts_pcm16(wave.get(),51200,pcm.get(),102400);
      if(!rc)rc=op.poll();
      if(!rc) {
        for(unsigned i=0;i<kept*256u;i++) {
          unsigned at=(skip*256u+i)*2;
          int value=static_cast<int16_t>(unsigned(pcm[at])|(unsigned(pcm[at+1])<<8));
          unsigned peak=value<0?-value:value;pcm_nonzero+=value!=0;
          if(peak>pcm_peak)pcm_peak=peak;
        }
        rc=sink(sink_cookie,pcm.get()+skip*512u,static_cast<std::size_t>(kept)*512);
        if(!rc){if(first_us<0)first_us=now()-start;samples+=kept*256u;++packets;}
      }
      if(!rc)rc=op.poll();
      first+=kept;
    }
    voice_acoustic_mel_release(s->acoustic,&features);
  }
  // Explicitly propagate close failures; no fd is retained across commands.
  if(op.client && !op.borrowed){int r=a733_tensor_client_close(op.client);op.client=nullptr;if(!rc)rc=r;}
  if(!rc)rc=op.poll();
  if(!rc && !packets)rc=-ENODATA;
  std::printf("tts-pcm-v212: samples=%u nonzero=%u peak=%u; before volume/resampling, NOT acoustic acceptance\n",samples,pcm_nonzero,pcm_peak);
  std::printf("local-tts: result=%d acoustic=CPU vocoder=NPU packets=%u samples=%u cpu-us=%lld npu-us=%lld first-sink-us=%lld; NOT end-of-speech/physical-audio latency\n",
      rc,packets,samples,cpu_us,npu_us,first_us);
  return rc;
}
}
