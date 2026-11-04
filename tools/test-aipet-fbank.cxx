/* SPDX-License-Identifier: Apache-2.0 */
#include "voice_fbank.hxx"
#include "online-feature.h"
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <vector>
using namespace aipet;
using Owner=std::unique_ptr<VoiceFbank,decltype(&voice_fbank_destroy)>;
static Owner create()
{
  VoiceFbank *s=nullptr; assert(voice_fbank_create(&s)==0 && s);
  return Owner(s,voice_fbank_destroy);
}
static std::vector<unsigned char> tone(unsigned n,double hz)
{
  std::vector<unsigned char> pcm(n*2);
  for(unsigned i=0;i<n;i++) {
    auto v=static_cast<unsigned short>(static_cast<short>(12000*std::sin(i*hz*6.283185307179586/16000)));
    pcm[2*i]=v&255; pcm[2*i+1]=v>>8;
  }
  return pcm;
}
static std::vector<float> extract(const std::vector<unsigned char> &pcm,unsigned chunk)
{
  auto s=create();
  for(std::size_t i=0;i<pcm.size();) {
    std::size_t n=std::min<std::size_t>(chunk*2,pcm.size()-i);
    assert(voice_fbank_accept(s.get(),pcm.data()+i,n)==0); i+=n;
  }
  assert(voice_fbank_finish(s.get())==0);
  assert(voice_fbank_finish(s.get())==0); // idempotent, never duplicate flush
  auto frames=voice_fbank_frames(s.get()); assert(frames>=0);
  std::vector<float> out(frames*80);
  assert(voice_fbank_copy(s.get(),0,frames,out.data(),out.size())==0);
  for(float v:out) assert(std::isfinite(v));
  return out;
}
int main()
{
  assert(voice_fbank_create(nullptr)==-EINVAL);
  assert(voice_fbank_accept(nullptr,nullptr,0)==-EINVAL);
  assert(voice_fbank_finish(nullptr)==-EINVAL && voice_fbank_frames(nullptr)==-EINVAL);
  voice_fbank_destroy(nullptr);
  auto empty=create(); assert(voice_fbank_finish(empty.get())==-ENODATA);
  assert(voice_fbank_accept(empty.get(),nullptr,0)==0);
  unsigned char bad[2]={0};
  assert(voice_fbank_accept(empty.get(),bad,1)==-EINVAL);
  assert(voice_fbank_accept(empty.get(),nullptr,2)==-EINVAL);
  for(unsigned n:{1u,79u,80u,100u,399u,400u,1600u,16000u,160000u}) {
    auto pcm=tone(n,1000);
    auto batch=extract(pcm,160000), stream=extract(pcm,97);
    assert(batch==stream && batch.size()==((n+80)/160)*80);
    /* Independent client of the same reference math: one-shot normalized
     * waveform, without the adapter's S16 decoder/chunk boundaries. */
    knf::FbankOptions o; o.mel_opts.num_bins=80; o.mel_opts.high_freq=-400;
    o.frame_opts.dither=0; o.frame_opts.snip_edges=false;
    knf::OnlineFbank reference(o);
    std::vector<float> floats(n);
    for(unsigned i=0;i<n;i++) {
      unsigned u=pcm[i*2] | unsigned(pcm[i*2+1])<<8;
      floats[i]=(u>=32768 ? int(u)-65536 : int(u))/32768.0f;
    }
    reference.AcceptWaveform(16000,floats.data(),n); reference.InputFinished();
    assert(unsigned(reference.NumFramesReady())==batch.size()/80);
    for(unsigned i=0;i<batch.size()/80;i++)
      for(unsigned j=0;j<80;j++) assert(batch[i*80+j]==reference.GetFrame(i)[j]);
  }
  std::vector<unsigned char> silence(32000,0);
  auto zero=extract(silence,16000);
  for(float v:zero) assert(std::abs(v-std::log(std::numeric_limits<float>::epsilon()))<1e-6f);
  auto a=extract(tone(16000,400),320),b=extract(tone(16000,2500),320);
  assert(a!=b); // changed input must change real features
  auto s=create(); float row[80];
  assert(voice_fbank_copy(s.get(),0,1,row,80)==-ERANGE);
  auto pcm=tone(160000,400);
  assert(voice_fbank_accept(s.get(),pcm.data(),pcm.size())==0);
  int before=voice_fbank_frames(s.get());
  assert(voice_fbank_accept(s.get(),bad,2)==-E2BIG && voice_fbank_frames(s.get())==before);
  assert(voice_fbank_finish(s.get())==0 && voice_fbank_frames(s.get())==1000);
  assert(voice_fbank_copy(s.get(),0,1,row,79)==-ENOSPC);
  assert(voice_fbank_copy(s.get(),1000,1,row,80)==-ERANGE);
  assert(voice_fbank_copy(s.get(),0,1,nullptr,80)==-EINVAL);
  assert(voice_fbank_accept(s.get(),bad,2)==-EPERM);
  puts("PASS: real Kaldi fbank, batch/stream exact, normalized reference parity, silence floor, changed tones, short/10s bounds, errors. CPU frontend only; NOT NPU/ASR proof.");
}
