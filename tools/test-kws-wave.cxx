/* SPDX-License-Identifier: Apache-2.0 */
/* Actual WAV parser + actual frontend vs Linux-validated features. CPU only. */
#include "../board/a733-cubie-a7z/openvela-overlay/apps/system/aipet/voice_fbank.hxx"
#include <system/a733_pcm.h>
#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <memory>
#include <new>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
static std::vector<char> expected;
static unsigned invoked;
static bool interrupted(){return false;}
extern "C" void aipet_wipe(void *p,size_t n){std::memset(p,0,n);}
extern "C" int a733_pcm_capture(unsigned,unsigned,unsigned char *,size_t,size_t *){return -ENODEV;}
namespace aipet {
static int audio_decimate(const unsigned char *,size_t,unsigned char *,size_t,size_t *){return -ENOTSUP;}
int voice_npu_kws_memory(const char *,const float *data,unsigned frames)
{assert(expected.size()==frames*320 && !std::memcmp(data,expected.data(),expected.size()));++invoked;return 0;}
#include "../board/a733-cubie-a7z/openvela-overlay/apps/system/aipet/voice_kws_input.inc"
}
static std::vector<char> read(const std::string &path)
{std::ifstream f(path,std::ios::binary|std::ios::ate);assert(f);auto n=f.tellg();assert(n>0 && n<1100000);std::vector<char> data(n);f.seekg(0);f.read(data.data(),n);assert(f);return data;}
int main(int argc,char **argv)
{
  assert(argc==3);(void)&aipet::kws_wav_file;(void)&aipet::kws_mic;
  for(unsigned i=0;i<6;++i) {
    auto wav=read(std::string(argv[1])+"/wave"+std::to_string(i)+".wav");
    expected=read(std::string(argv[2])+"/features"+std::to_string(i)+".f32");
    const unsigned char *pcm=nullptr;size_t bytes=0;unsigned rate=0;
    auto source=reinterpret_cast<const unsigned char *>(wav.data());
    assert(!aipet::kws_wave(source,wav.size(),&pcm,&bytes,&rate) && rate==16000);
    assert(!aipet::kws_pcm("mock",pcm,bytes,rate));
    for(size_t n=0;n<44;++n)assert(aipet::kws_wave(source,n,&pcm,&bytes,&rate)<0);
    wav[4]^=1;assert(aipet::kws_wave(source,wav.size(),&pcm,&bytes,&rate)<0);wav[4]^=1;
    wav[22]=2;assert(aipet::kws_wave(source,wav.size(),&pcm,&bytes,&rate)<0);wav[22]=1;
    wav[24]=0;assert(aipet::kws_wave(source,wav.size(),&pcm,&bytes,&rate)<0);
  }
  assert(invoked==6);std::puts("Actual WAV + fbank: SIX Linux fixture feature buffers byte-exact; truncated/RIFF/stereo/rate rejected; CPU ONLY");
}
