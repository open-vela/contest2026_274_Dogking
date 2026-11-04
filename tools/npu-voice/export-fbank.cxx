/* SPDX-License-Identifier: Apache-2.0 */
/* Host fixture exporter using the exact v153 application frontend. No NPU. */
#include "voice_fbank.hxx"
#include "online-feature.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

int main(int argc,char **argv)
{
  if(argc!=3 && !(argc==4 && !std::strcmp(argv[3],"--pcm-units"))) {
    std::fprintf(stderr,"usage: export-fbank RAW_S16LE_16K_MONO NEW_F32 [--pcm-units]\n"); return 2;
  }
  unsigned short endian=1;
  if(*reinterpret_cast<unsigned char *>(&endian)!=1 || sizeof(float)!=4) return 2;
  int input=open(argv[1],O_RDONLY|O_NOFOLLOW);
  if(input<0) return 2;
  struct stat st{};
  if(fstat(input,&st) || !S_ISREG(st.st_mode) || st.st_size<=0 || st.st_size%2 ||
     st.st_size>320000) { close(input); return 2; }
  std::vector<unsigned char> pcm(static_cast<std::size_t>(st.st_size));
  std::size_t at=0;
  while(at<pcm.size()) {
    ssize_t n=read(input,pcm.data()+at,pcm.size()-at);
    if(n<0 && errno==EINTR) continue;
    if(n<=0) { close(input); return 2; }
    at+=static_cast<std::size_t>(n);
  }
  if(close(input)) return 2;
  int frames=0;
  std::vector<float> features;
  if(argc==4) {
    // Explicit scale-diagnostic alternative, never changes the v153 app API.
    knf::FbankOptions o;
    o.frame_opts.samp_freq=16000; o.frame_opts.dither=0; o.frame_opts.snip_edges=false;
    o.mel_opts.num_bins=80; o.mel_opts.high_freq=-400;
    knf::OnlineFbank bank(o);
    std::vector<float> samples(pcm.size()/2);
    for(std::size_t i=0;i<samples.size();i++) {
      unsigned u=unsigned(pcm[2*i]) | unsigned(pcm[2*i+1])<<8;
      samples[i]=static_cast<float>(u>=32768 ? int(u)-65536 : int(u));
    }
    bank.AcceptWaveform(16000,samples.data(),samples.size()); bank.InputFinished();
    frames=bank.NumFramesReady();
    if(frames<29 || frames>1000) return 2;
    features.resize(static_cast<std::size_t>(frames)*80);
    for(int i=0;i<frames;i++) std::memcpy(features.data()+i*80,bank.GetFrame(i),320);
  } else {
  aipet::VoiceFbank *raw=nullptr;
  if(aipet::voice_fbank_create(&raw)) return 2;
  std::unique_ptr<aipet::VoiceFbank,decltype(&aipet::voice_fbank_destroy)>
      owner(raw,aipet::voice_fbank_destroy);
  if(aipet::voice_fbank_accept(raw,pcm.data(),pcm.size()) || aipet::voice_fbank_finish(raw)) return 2;
  frames=aipet::voice_fbank_frames(raw);
  if(frames<29 || frames>1000) return 2;
  features.resize(static_cast<std::size_t>(frames)*80);
  if(aipet::voice_fbank_copy(raw,0,frames,features.data(),features.size())) return 2;
  }
  for(float v:features) if(!std::isfinite(v)) return 2;
  int out=open(argv[2],O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
  if(out<0) return 2;
  const auto *bytes=reinterpret_cast<const unsigned char *>(features.data());
  std::size_t size=features.size()*sizeof(float); at=0;
  while(at<size) {
    ssize_t n=write(out,bytes+at,size-at);
    if(n<0 && errno==EINTR) continue;
    if(n<=0) { close(out); return 2; }
    at+=static_cast<std::size_t>(n);
  }
  int sync=fsync(out),closed=close(out);
  if(sync || closed) return 2;
  std::printf("CPU-only fbank frames=%d bins=80 samples=%zu\n",frames,pcm.size()/2);
  return 0;
}
