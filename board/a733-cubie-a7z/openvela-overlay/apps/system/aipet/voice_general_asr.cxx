/* SPDX-License-Identifier: Apache-2.0 */
#include "voice_general_asr.hxx"
#include "voice_general_contract.hxx"
#include "voice_compute.hxx"
#include "voice_load_log.hxx"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <new>
#include <utility>
namespace aipet {
namespace {
struct Native {
  a733_tensor_client_contract shape{};
  a733_tensor_client *client=nullptr;
  a733_tensor_client_input input[64]{};
  a733_tensor_client_output output[64]{};
  unsigned calls=0;long long elapsed_us=0;
};
long long clock_us() {
  timespec now{};clock_gettime(CLOCK_MONOTONIC,&now);
  return static_cast<long long>(now.tv_sec)*1000000+now.tv_nsec/1000;
}
bool finite(const void *data,std::size_t bytes) {
  auto *p=static_cast<const float *>(data);
  for(std::size_t i=0;i<bytes/4;i++) if(!std::isfinite(p[i])) return false;
  return bytes%4==0;
}
}
struct VoiceGeneralAsr {
  Native model[3];
  VoiceAsrStream *stream=nullptr;
  char *pieces[6257]{};
  long long deadline=0;
  VoiceLocalCancel cancel=nullptr;
  void *cancel_cookie=nullptr;
  int error=0;
  unsigned accept_calls=0;
  long long accept_us=0,finish_us=0;
  unsigned char pending_pcm[1280]{}; // 40ms at 16k mono S16; no dropped tail.
  unsigned pending_bytes=0,received_samples=0,compute_batches=0;
  bool finished=false,discarded=false;
};
namespace {
int stopped(void *cookie) {
  auto *s=static_cast<VoiceGeneralAsr *>(cookie);
  if(s->error) return s->error;
  if(s->cancel && s->cancel(s->cancel_cookie)) return s->error=-ECANCELED;
  if(clock_us()>=s->deadline) return s->error=-ETIMEDOUT;
  return 0;
}
int execute(VoiceGeneralAsr *s,unsigned index) {
  int rc=stopped(s);if(rc)return rc;
  long long left=(s->deadline-clock_us()+999)/1000;
  if(left<=0)return s->error=-ETIMEDOUT;
  auto &m=s->model[index];
  const long long at=clock_us();
  rc=a733_tensor_client_execute(m.client,m.input,m.output,
                                static_cast<uint32_t>(left>5000?5000:left),0);
  ++m.calls;m.elapsed_us+=clock_us()-at;
  if(rc)return s->error=rc;
  for(unsigned i=0;i<m.shape.outputs;i++)
    if(!finite(m.output[i].data,m.output[i].bytes))return s->error=-EILSEQ;
  return stopped(s);
}
int begin(void *cookie) {
  auto *s=static_cast<VoiceGeneralAsr *>(cookie);
  for(auto &m:s->model) {
    for(unsigned i=0;i<m.shape.inputs;i++)
      std::memset(const_cast<void *>(m.input[i].data),0,m.input[i].bytes);
    for(unsigned i=0;i<m.shape.outputs;i++)std::memset(m.output[i].data,0,m.output[i].bytes);
  }
  return stopped(s);
}
void end(void *) {}
int encode(void *cookie,const float *features,float *rows) {
  auto *s=static_cast<VoiceGeneralAsr *>(cookie);auto &m=s->model[0];
  std::memcpy(const_cast<void *>(m.input[0].data),features,32960);
  int rc=execute(s,0);if(rc)return rc;
  std::memcpy(rows,m.output[0].data,49152);
  // All 35 outputs, including length/cache states, feed the SAME native input
  // index. Swap CPU buffers only after successful complete tensor publication.
  for(unsigned i=1;i<36;i++) {
    void *old=const_cast<void *>(m.input[i].data);
    m.input[i].data=m.output[i].data;m.output[i].data=old;
  }
  return 0;
}
int decoder(void *cookie,const int64_t context[2],float *out) {
  auto *s=static_cast<VoiceGeneralAsr *>(cookie);auto &m=s->model[1];
  float native[2];
  for(unsigned i=0;i<2;i++) {
    if(context[i]<0 || context[i]>=6254)return s->error=-ERANGE;
    native[i]=static_cast<float>(context[i]); // exact, qualified native F32 IDs
  }
  std::memcpy(const_cast<void *>(m.input[0].data),native,sizeof(native));
  int rc=execute(s,1);if(!rc)std::memcpy(out,m.output[0].data,2048);
  return rc;
}
int joiner(void *cookie,const float *enc,const float *dec,float *out) {
  auto *s=static_cast<VoiceGeneralAsr *>(cookie);auto &m=s->model[2];
  std::memcpy(const_cast<void *>(m.input[0].data),enc,2048);
  std::memcpy(const_cast<void *>(m.input[1].data),dec,2048);
  int rc=execute(s,2);if(!rc)std::memcpy(out,m.output[0].data,25016);
  return rc;
}
int path(char *out,std::size_t cap,const char *dir,const char *file) {
  if(!dir || !*dir)return -EINVAL;
  int n=std::snprintf(out,cap,"%s/%s",dir,file);
  return n<0 || static_cast<std::size_t>(n)>=cap ? -ENAMETOOLONG:0;
}
int tokens(VoiceGeneralAsr *s,const char *dir) {
  char name[128],line[512];int rc=path(name,sizeof(name),dir,"tokens.txt");
  if(rc)return rc;
  FILE *f=std::fopen(name,"rb");if(!f) {
    int rc=-errno;std::printf("asr-v210: stage=tokens-open path=%s error=%d\n",name,rc);return rc;
  }
  unsigned count=0,total=0;
  while(!rc && std::fgets(line,sizeof(line),f)) {
    std::size_t n=std::strlen(line);total+=n;
    if(!n || line[n-1]!='\n' || total>512*1024u){rc=-EILSEQ;break;}
    while(n && (line[n-1]=='\n'||line[n-1]=='\r'))line[--n]=0;
    char *sep=std::strrchr(line,' ');if(!sep)sep=std::strrchr(line,'\t');
    if(!sep || sep==line || sep-line>256){rc=-EILSEQ;break;}
    *sep++=0;char *tail=nullptr;errno=0;
    unsigned long id=std::strtoul(sep,&tail,10);
    if(errno || !*sep || !tail || *tail || id>=6257 || s->pieces[id]){rc=-EILSEQ;break;}
    std::size_t bytes=std::strlen(line)+1;
    s->pieces[id]=static_cast<char *>(std::malloc(bytes));
    if(!s->pieces[id]){rc=-ENOMEM;break;}
    std::memcpy(s->pieces[id],line,bytes);++count;
  }
  if(!rc && (std::ferror(f)||count!=6257))rc=-EILSEQ;
  if(std::fclose(f) && !rc)rc=-EIO;
  return rc;
}
}
int voice_general_asr_open(const char *dir,long long deadline,VoiceLocalCancel cancel,
                           void *cookie,VoiceGeneralAsr **out) {
  if(!out)return -EINVAL;
  *out=nullptr;
  if(deadline<=clock_us() || deadline-clock_us()>120000000LL)return -EINVAL;
  auto *s=new(std::nothrow) VoiceGeneralAsr;if(!s)return -ENOMEM;
  s->deadline=deadline;s->cancel=cancel;s->cancel_cookie=cookie;
  // Include package parsing/CRC in the scoped A76 window, not only stream
  // creation. No persistent pin, cross-group fd reuse, or skipped validation.
  VoiceCompute compute(6);
  int rc=compute.begin();
  const long long token_at=clock_us();
  if(!rc) {
    VoiceLoadLog load("asr-tokens",dir);rc=tokens(s,dir);load.finish(rc);
  }
  std::printf("asr-prepare-v213: tokens-us=%lld status=%d\n",clock_us()-token_at,rc);
  const char *roles[]={"asr-encoder","asr-decoder","asr-joiner"};
  for(unsigned r=0;!rc && r<3;r++) {
    auto &m=s->model[r];char filename[32]{},name[128]{};
    rc=stopped(s);if(rc)break;
    rc=voice_general_contract(roles[r],m.shape);if(rc)break;
    for(unsigned i=0;!rc && i<m.shape.inputs;i++) {
      void *p=std::calloc(1,m.shape.input_bytes[i]);m.input[i]={p,m.shape.input_bytes[i]};
      if(!p)rc=-ENOMEM;
    }
    for(unsigned i=0;!rc && i<m.shape.outputs;i++) {
      void *p=std::calloc(1,m.shape.output_bytes[i]);m.output[i]={p,m.shape.output_bytes[i]};
      if(!p)rc=-ENOMEM;
    }
    std::snprintf(filename,sizeof(filename),"%s.a7tv",roles[r]);
    if(!rc)rc=path(name,sizeof(name),dir,filename);
    const long long query_at=clock_us();
    if(!rc) {
      VoiceLoadLog load("asr-native-query",name);
      rc=a733_tensor_client_open(name,&m.shape,&m.client);load.finish(rc);
    }
    const long long prepare_at=clock_us();
    if(!rc) {
      VoiceLoadLog load("asr-native-prepare",name);
      rc=a733_tensor_client_prepare(m.client);load.finish(rc);
    }
    std::printf("asr-prepare-v213: role=%s query-alloc-us=%lld prepare-us=%lld status=%d\n",
                roles[r],prepare_at-query_at,clock_us()-prepare_at,rc);
    if(rc)std::printf("asr-v210: stage=model-open-prepare role=%s path=%s error=%d\n",roles[r],name,rc);
  }
  VoiceAsrStreamOps ops{begin,encode,end,{decoder,joiner,stopped}};
  if(!rc)rc=voice_asr_stream_open(&ops,s,&s->stream);
  rc=compute.finish(rc);
  if(rc){voice_general_asr_close(s);return rc;}
  *out=s;return 0;
}
int voice_general_asr_after_idle(VoiceGeneralAsr *s,long long deadline) {
  if(!s || s->error || s->finished || s->discarded || s->received_samples || s->pending_bytes)
    return -EBUSY;
  long long at=clock_us();
  if(deadline<=at || deadline-at>120000000LL)return -EINVAL;
  if(s->cancel && s->cancel(s->cancel_cookie))return -ECANCELED;
  s->deadline=deadline;return 0;
}
int voice_general_asr_accept(VoiceGeneralAsr *s,const unsigned char *p,std::size_t n) {
  if(!s)return -EINVAL;
  if(s->discarded)return -EBUSY;
  int rc=stopped(s);if(rc)return rc;
  if(s->finished)return -EALREADY;
  if(!p || !n || n%2 || n/2>128000-s->received_samples)return s->error=-EINVAL;
  s->received_samples+=n/2;
  long long start=clock_us();
  VoiceCompute compute(6);
  bool computing=n+ s->pending_bytes>=sizeof(s->pending_pcm);
  if(computing){rc=compute.begin();++s->compute_batches;}
  while(!rc && n) {
    unsigned take=sizeof(s->pending_pcm)-s->pending_bytes;
    if(take>n)take=n;
    std::memcpy(s->pending_pcm+s->pending_bytes,p,take);
    s->pending_bytes+=take;p+=take;n-=take;
    if(s->pending_bytes==sizeof(s->pending_pcm)) {
      rc=voice_asr_stream_accept(s->stream,s->pending_pcm,s->pending_bytes);
      s->pending_bytes=0;
    }
  }
  if(computing)rc=compute.finish(rc);
  ++s->accept_calls;s->accept_us+=clock_us()-start;
  return rc ? s->error=rc:stopped(s);
}
int voice_general_asr_finish(VoiceGeneralAsr *s) {
  if(!s)return -EINVAL;
  if(s->discarded)return -EBUSY;
  long long start=clock_us();
  VoiceCompute compute(6);int rc=compute.begin();
  if(!rc)rc=stopped(s);
  if(!rc && s->pending_bytes) {
    rc=voice_asr_stream_accept(s->stream,s->pending_pcm,s->pending_bytes);
    s->pending_bytes=0;
  }
  if(!rc)rc=voice_asr_stream_finish(s->stream);
  rc=compute.finish(rc);
  if(!rc)s->finished=true;
  s->finish_us=clock_us()-start;
  return rc ? s->error=rc:stopped(s);
}
int voice_general_asr_text(VoiceGeneralAsr *s,char *out,std::size_t capacity) {
  if(!s || !out || !capacity || capacity>8192)return -EINVAL;
  if(s->discarded)return -EBUSY;
  int rc=stopped(s);if(rc)return rc;
  std::unique_ptr<unsigned[]> ids(new(std::nothrow) unsigned[AIPET_ASR_MAX_TOKENS]);
  std::unique_ptr<unsigned[]> positions(new(std::nothrow) unsigned[AIPET_ASR_MAX_TOKENS]);
  std::unique_ptr<char[]> text(new(std::nothrow) char[capacity]);
  if(!ids || !positions || !text)return -ENOMEM;
  std::size_t used=0,n=0;
  rc=voice_asr_stream_result(s->stream,ids.get(),positions.get(),AIPET_ASR_MAX_TOKENS,&used);
  for(std::size_t i=0;!rc && i<used;i++) {
    if(ids[i]>=6254 || !s->pieces[ids[i]])return -EILSEQ;
    const auto *p=reinterpret_cast<const unsigned char *>(s->pieces[ids[i]]);
    while(*p) {
      unsigned char c=*p++;
      if(c==0xe2 && p[0]==0x96 && p[1]==0x81){c=' ';p+=2;}
      if(c==' ' && n==0)continue;
      if(n>=capacity-1)return -ENOSPC;
      text[n++]=static_cast<char>(c);
    }
  }
  if(rc)return rc;
  while(n && text[n-1]==' ')--n;
  text[n]=0;
  if((rc=stopped(s)))return rc;
  std::memcpy(out,text.get(),n+1);return 0;
}
int voice_general_asr_stats(VoiceGeneralAsr *s,VoiceAsrStreamStats *out) {
  int rc=s?voice_asr_stream_stats(s->stream,out):-EINVAL;
  if(!rc)out->samples=s->received_samples;
  return rc;
}
int voice_general_asr_discard(VoiceGeneralAsr *s) {
  if(!s)return -EINVAL;
  int rc=stopped(s);if(rc)return rc;
  if(s->discarded || s->finished)return -EALREADY;
  if(!s->stream)return -EINVAL;
  // Caller has joined the consumer; all execute() calls are synchronous and
  // returned success. stream close invokes CPU cleanup only (end is a no-op).
  // Do not finish(), submit another tensor, reopen models or reset hardware.
  voice_asr_stream_close(s->stream);s->stream=nullptr;s->discarded=true;
  std::memset(s->pending_pcm,0,sizeof(s->pending_pcm));s->pending_bytes=0;
  return 0;
}
int voice_general_asr_reset(VoiceGeneralAsr *s,long long deadline,
                             VoiceLocalCancel cancel,void *cookie) {
  if(!s || deadline<=clock_us() || deadline-clock_us()>120000000LL)return -EINVAL;
  if(s->error)return s->error;
  VoiceAsrStreamStats stats{};int rc=s->discarded?0:voice_asr_stream_stats(s->stream,&stats);
  if(rc)return rc;
  if(!s->discarded && !stats.finished)return -EBUSY;
  voice_asr_stream_close(s->stream);s->stream=nullptr;
  s->deadline=deadline;s->cancel=cancel;s->cancel_cookie=cookie;
  s->accept_calls=0;s->accept_us=s->finish_us=0;
  s->pending_bytes=s->received_samples=s->compute_batches=0;s->finished=false;s->discarded=false;
  std::memset(s->pending_pcm,0,sizeof(s->pending_pcm));
  for(auto &m:s->model){m.calls=0;m.elapsed_us=0;}
  VoiceAsrStreamOps ops{begin,encode,end,{decoder,joiner,stopped}};
  VoiceCompute compute(6);rc=compute.begin();
  if(!rc)rc=voice_asr_stream_open(&ops,s,&s->stream);
  rc=compute.finish(rc);
  return rc?s->error=rc:0;
}
int voice_general_asr_close(VoiceGeneralAsr *s) {
  if(!s)return 0;
#ifdef __NuttX__
  const char *compute="scoped CPU6, double MIDR/MPIDR proof; NPU unchanged";
#else
  const char *compute="HOST; no target affinity/inference qualification";
#endif
  std::printf("asr-compute-v190: encoder=%u/%lldus decoder=%u/%lldus joiner=%u/%lldus; %s\n",
    s->model[0].calls,s->model[0].elapsed_us,s->model[1].calls,s->model[1].elapsed_us,
    s->model[2].calls,s->model[2].elapsed_us,compute);
  std::printf("asr-v210: accept-batches=%u accept-us=%lld final-flush-us=%lld status=%d; nested NPU times are not additive and not E2E latency\n",
    s->accept_calls,s->accept_us,s->finish_us,s->error);
  VoiceAsrStreamStats stats{};(void)voice_asr_stream_stats(s->stream,&stats);
  std::printf("asr-v211: received=%u staged-bytes=%u qualified-batches=%u tail-frames=%u max-stage-ms=40; no dropped PCM\n",
    s->received_samples,s->pending_bytes,s->compute_batches,stats.padded_frames);
  volatile unsigned char *wipe=s->pending_pcm;
  for(unsigned i=0;i<sizeof(s->pending_pcm);++i)wipe[i]=0;
  voice_asr_stream_close(s->stream);int rc=0;
  for(auto &m:s->model) {
    if(m.client){int r=a733_tensor_client_close(m.client);if(r && !rc)rc=r;}
    for(auto &i:m.input)std::free(const_cast<void *>(i.data));
    for(auto &o:m.output)std::free(o.data);
  }
  for(auto *p:s->pieces)std::free(p);
  delete s;return rc;
}
}
