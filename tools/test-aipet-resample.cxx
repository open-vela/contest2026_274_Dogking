/* SPDX-License-Identifier: Apache-2.0 */
#include "audio_resample.hxx"
#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
static void le16(unsigned char *p,unsigned n) { p[0]=n&255; p[1]=(n>>8)&255; }
static void le32(unsigned char *p,unsigned n) { le16(p,n&65535); le16(p+2,n>>16); }
static std::vector<unsigned char> fixture(unsigned frames, double hz=0)
{
  std::vector<unsigned char> out(44+frames*2);
  std::memcpy(out.data(),"RIFF",4); le32(out.data()+4,out.size()-8);
  std::memcpy(out.data()+8,"WAVEfmt ",8); le32(out.data()+16,16);
  le16(out.data()+20,1); le16(out.data()+22,1); le32(out.data()+24,22050);
  le32(out.data()+28,44100); le16(out.data()+32,2); le16(out.data()+34,16);
  std::memcpy(out.data()+36,"data",4); le32(out.data()+40,frames*2);
  for(unsigned i=0;i<frames;i++) {
    int sample=hz ? std::lround(10000*std::sin(2*3.141592653589793*hz*i/22050)) : -1234;
    le16(out.data()+44+i*2,static_cast<uint16_t>(sample));
  }
  return out;
}
static int sample(const unsigned char *p) { unsigned n=p[0]|(unsigned(p[1])<<8); return n>=32768 ? int(n)-65536 : int(n); }
int main()
{
  std::vector<unsigned char> output(960000,0xcc);
  std::size_t used=9;
  for(unsigned frames:{1u,2205u,22050u,220500u}) {
    auto wav=fixture(frames);
    assert(aipet::matcha_wav_to_48k(wav.data(),wav.size(),output.data(),output.size(),&used)==0);
    assert(used==((std::size_t(frames)*320+146)/147)*2);
    for(std::size_t i=0;i<used;i+=2) assert(sample(output.data()+i)==-1234);
  }
  auto tone=fixture(22050,1000);
  assert(aipet::matcha_wav_to_48k(tone.data(),tone.size(),output.data(),output.size(),&used)==0);
  assert(used==96000);
  double power=0,error=0;
  for(unsigned i=100;i<47900;i++) {
    double value=sample(output.data()+i*2), expected=10000*std::sin(2*3.141592653589793*1000*i/48000);
    power+=value*value; error+=(value-expected)*(value-expected);
  }
  double rms=std::sqrt(power/47800), rmse=std::sqrt(error/47800);
  assert(rms>7000 && rms<7150 && rmse<30);
  auto wav=fixture(2205);
  // Unknown odd-sized chunk (with RIFF pad) must not shift the sample data.
  wav.insert(wav.begin()+36,{'J','U','N','K',1,0,0,0,0x55,0}); le32(wav.data()+4,wav.size()-8);
  assert(aipet::matcha_wav_to_48k(wav.data(),wav.size(),output.data(),output.size(),&used)==0 && used==9600);
  assert(aipet::matcha_wav_to_48k(wav.data(),wav.size(),output.data(),9599,&used)==-ENOSPC && used==0);
  auto overlong=fixture(220501);
  assert(aipet::matcha_wav_to_48k(overlong.data(),overlong.size(),output.data(),output.size(),&used)<0 && used==0);
  for(unsigned scenario=0;scenario<9;scenario++) {
    auto bad=fixture(2205);
    switch(scenario) {
      case 0: bad[0]='X'; break;
      case 1: le32(bad.data()+4,0xffffffff); break;
      case 2: le32(bad.data()+24,16000); break;
      case 3: le16(bad.data()+22,2); break;
      case 4: le16(bad.data()+20,3); break;
      case 5: le16(bad.data()+34,24); break;
      case 6: le32(bad.data()+40,0xffffffff); break;
      case 7: bad.pop_back(); break;
      case 8: le32(bad.data()+28,48000); break;
    }
    assert(aipet::matcha_wav_to_48k(bad.data(),bad.size(),output.data(),output.size(),&used)<0 && used==0);
  }
  assert(aipet::matcha_wav_to_48k(nullptr,44,output.data(),output.size(),&used)<0);
  std::printf("Matcha 22.05k->48k: 1kHz RMS=%.2f RMSE=%.2f; DC/10s/bounds/RIFF tests PASS (host only)\n",rms,rmse);
}
