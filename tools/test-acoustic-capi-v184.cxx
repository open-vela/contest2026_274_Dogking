/* SPDX-License-Identifier: Apache-2.0 */
/* HOST CPU oracle only; never linked into firmware. */
#include "voice_acoustic.hxx"
#include "voice_tts_frontend.hxx"
#include <cassert>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <thread>
#include <ctime>
static std::atomic<int> stop_code{0};
static int stopped(){return stop_code.load();}
static long long now(){timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);return static_cast<long long>(t.tv_sec)*1000000+t.tv_nsec/1000;}
static void write(const std::string &name,const void *data,std::size_t size) {
  FILE *f=std::fopen(name.c_str(),"wb");assert(f);
  assert(std::fwrite(data,1,size,f)==size);assert(!std::fclose(f));
}
int main(int argc,char **argv) {
  assert(argc==5);
  aipet::VoiceAcoustic *engine=nullptr;
  int rc=aipet::voice_acoustic_open(argv[1],&engine);
  if(rc)std::fprintf(stderr,"acoustic open=%d\n",rc);
  assert(!rc && engine);
  aipet::VoiceTtsFrontend *front=nullptr;
  std::string base=argv[2],out=argv[3];
  assert(!aipet::voice_tts_frontend_open((base+"/tokens.txt").c_str(),(base+"/lexicon.txt").c_str(),&front));
  const char *texts[]={"你好","测试成功","打开灯","现在几点了","一加一等于二"};
  for(unsigned i=0;i<5;i++) {
    std::vector<std::vector<int64_t>> sentences;
    assert(!aipet::voice_tts_frontend_text(front,texts[i],sentences) && sentences.size()==1);
    const auto &ids=sentences[0];aipet::VoiceAcousticMel mel{},other{};
    rc=aipet::voice_acoustic_run(engine,ids.data(),ids.size(),0,1,&mel);
    if(rc)std::fprintf(stderr,"acoustic run=%d case=%u\n",rc,i);
    assert(!rc && mel.data && mel.frames>0 && mel.frames<=861);
    assert(aipet::voice_acoustic_run(engine,ids.data(),ids.size(),0,1,&other)==-EBUSY && !other.tensor);
    auto *kept=mel.data;
    assert(aipet::voice_acoustic_run(engine,ids.data(),ids.size(),0,1,&mel)==-EBUSY && mel.data==kept);
    auto stem=out+"/case"+std::to_string(i);
    write(stem+".ids",ids.data(),ids.size()*8);
    write(stem+".mel",mel.data,80u*mel.frames*4u);
    std::printf("case=%u frames=%u ids=%zu text=%s CPU_ONLY\n",i,mel.frames,ids.size(),texts[i]);
    aipet::voice_acoustic_mel_release(engine,&mel);assert(!mel.tensor && !mel.data);
    assert(aipet::voice_acoustic_run(engine,ids.data(),0,0,1,&other)==-EINVAL);
    assert(aipet::voice_acoustic_run(engine,ids.data(),ids.size(),std::numeric_limits<float>::quiet_NaN(),1,&other)==-EINVAL);
  }
  std::vector<std::vector<int64_t>> sentences;
  assert(!aipet::voice_tts_frontend_text(front,"本地语音测试成功",sentences) && sentences.size()==1);
  auto &ids=sentences[0];aipet::VoiceAcousticMel mel{};
  assert(aipet::voice_acoustic_run(engine,ids.data(),ids.size(),0,1,&mel,now()-1)==-ETIMEDOUT);
  stop_code.store(-ECANCELED);
  assert(aipet::voice_acoustic_run(engine,ids.data(),ids.size(),0,1,&mel,0,stopped)==-ECANCELED);
  stop_code.store(0);
  std::thread cancel([]{std::this_thread::sleep_for(std::chrono::milliseconds(2));stop_code.store(-ECANCELED);});
  rc=aipet::voice_acoustic_run(engine,ids.data(),ids.size(),0,1,&mel,0,stopped);
  cancel.join();assert(rc==-ECANCELED && !mel.tensor);
  stop_code.store(0);
  assert(!aipet::voice_acoustic_run(engine,ids.data(),ids.size(),0,1,&mel,0,stopped));
  aipet::voice_acoustic_mel_release(engine,&mel);
  std::puts("PASS: deadline, pre-cancel, mid-run termination and same-session recovery (host CPU)");
  aipet::voice_tts_frontend_close(front);aipet::voice_acoustic_close(engine);
  std::puts("PASS: C API CPU mel generation; NOT NPU vocoder, RTOS or speaker acceptance");
}
