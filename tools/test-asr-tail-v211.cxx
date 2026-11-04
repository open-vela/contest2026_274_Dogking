/* SPDX-License-Identifier: Apache-2.0 */
#include "voice_asr_stream.hxx"
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>
namespace aipet {
struct OldVoiceAsrStream;
int old_open(const VoiceAsrStreamOps *,void *,OldVoiceAsrStream **);
int old_accept(OldVoiceAsrStream *,const unsigned char *,std::size_t);
int old_finish(OldVoiceAsrStream *);
void old_close(OldVoiceAsrStream *);
}
using namespace aipet;
struct Trace{std::vector<float> features;};
static int begin(void *){return 0;}
static void end(void *){}
static int encode(void *p,const float *input,float *out) {
  auto &v=static_cast<Trace *>(p)->features;v.insert(v.end(),input,input+103*80);
  std::fill(out,out+24*512,0);return 0;
}
static int decoder(void *,const int64_t *,float *out){std::fill(out,out+512,0);return 0;}
static int joiner(void *,const float *,const float *,float *out){std::fill(out,out+6254,-1);out[0]=1;return 0;}
static int cancel(void *){return 0;}
int main() {
  const VoiceAsrStreamOps ops={begin,encode,end,{decoder,joiner,cancel}};
  unsigned cases=0,saved=0;long long old_us=0,new_us=0;
  for(unsigned n:{1u,79u,80u,159u,160u,161u,15279u,15280u,15359u,15360u,15361u,16480u,30720u,48000u,128000u})
  for(unsigned step:{160u,640u,1600u})for(unsigned kind=0;kind<2;++kind) {
    std::vector<unsigned char> pcm(n*2);uint32_t rng=211;
    if(kind)for(auto &b:pcm){rng=rng*1664525+1013904223;b=rng>>24;}
    Trace a,b;OldVoiceAsrStream *old=nullptr;VoiceAsrStream *fresh=nullptr;
    assert(!old_open(&ops,&a,&old) && !voice_asr_stream_open(&ops,&b,&fresh));
    for(unsigned at=0;at<n;) {
      unsigned take=std::min(step,n-at);
      assert(!old_accept(old,pcm.data()+at*2,take*2));
      assert(!voice_asr_stream_accept(fresh,pcm.data()+at*2,take*2));at+=take;
    }
    auto start=std::chrono::steady_clock::now();int old_result=old_finish(old);
    auto middle=std::chrono::steady_clock::now();int new_result=voice_asr_stream_finish(fresh);
    assert(old_result==new_result && (new_result==0 || new_result==-ENODATA));
    auto stop=std::chrono::steady_clock::now();
    old_us+=std::chrono::duration_cast<std::chrono::microseconds>(middle-start).count();
    new_us+=std::chrono::duration_cast<std::chrono::microseconds>(stop-middle).count();
    assert(a.features.size()==b.features.size());
    assert(a.features.empty() || !std::memcmp(a.features.data(),b.features.data(),a.features.size()*sizeof(float)));
    VoiceAsrStreamStats stats{};assert(voice_asr_stream_stats(fresh,&stats)==new_result);
    assert(stats.padded_frames<=103);saved+=103-stats.padded_frames;
    old_close(old);voice_asr_stream_close(fresh);++cases;
  }
  assert(saved>0);
  std::printf("PASS %u cases all encoder-input float bytes/chunks exact; unused tail frames avoided=%u; HOST aggregate finish us old=%lld new=%lld (NOT A733 latency)\n",cases,saved,old_us,new_us);
}
