/* SPDX-License-Identifier: Apache-2.0 */
/* HOST interface test: REAL CPU acoustic, STUB NPU, NO audio/hardware proof. */
#include "voice_tts_engine.hxx"
#include <system/a733_tensor_client.h>
#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <limits>
#include <string>
#include <vector>
#include <unistd.h>
static pid_t fake_group=0;
extern "C" pid_t __real_getpid();
extern "C" pid_t __wrap_getpid(){return fake_group?fake_group:__real_getpid();}
static unsigned opens=0,closes=0,calls=0,outputs=0,expected_frames=0;
static int fail_run=0,fail_sink=0;
static bool bad_tail=false,prepared=false;
static bool check_golden=true;
static bool new_text=false; // only v214 Latin/OOV: no old Chinese golden exists
static std::vector<float> expected_mel;
static long long now(){timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000000LL+t.tv_nsec/1000;}
struct a733_tensor_client { unsigned sentinel=185; };
extern "C" int a733_tensor_client_open(const char *path,
    const a733_tensor_client_contract *c,a733_tensor_client **out) {
  assert(std::string(path).find("vocoder.a7tv")!=std::string::npos);
  assert(c->inputs==1 && c->outputs==1 && c->input_bytes[0]==64000 && c->output_bytes[0]==204800);
  *out=new a733_tensor_client;prepared=false;++opens;return 0;
}
extern "C" int a733_tensor_client_prepare(a733_tensor_client *c) {
  assert(c && c->sentinel==185);assert(!prepared);prepared=true;return 0;
}
extern "C" int a733_tensor_client_execute(a733_tensor_client *c,
    const a733_tensor_client_input *in,const a733_tensor_client_output *out,unsigned ms,int verify) {
  assert(c && prepared && ms>0 && ms<=5000 && verify==0);
  assert(in->bytes==64000 && out->bytes==204800);++calls;
  if(fail_run)return fail_run;
  const auto *mel=static_cast<const float *>(in->data);
  for(unsigned channel=0;channel<80;channel++)for(unsigned frame=0;frame<200;frame++) {
    float wanted=frame<expected_frames?expected_mel[channel*expected_frames+frame]:-11.4f;
    assert(std::isfinite(mel[channel*200+frame]));
    if(!new_text && (check_golden || frame>=expected_frames))assert(mel[channel*200+frame]==wanted);
  }
  // Deliberately ZERO output for contract tests, never save/present as audio.
  std::memset(out->data,0,out->bytes);
  if(bad_tail)static_cast<float *>(out->data)[51199]=std::numeric_limits<float>::quiet_NaN();
  return 0;
}
extern "C" int a733_tensor_client_close(a733_tensor_client *c) {
  assert(c && c->sentinel==185);c->sentinel=0;delete c;++closes;return 0;
}
static int sink(void *,const unsigned char *pcm,std::size_t bytes) {
  assert(pcm && (new_text?(bytes>0&&bytes<=102400&&bytes%512==0):bytes==expected_frames*512u));
  if(fail_sink)return fail_sink;
  for(std::size_t i=0;i<bytes;i++)assert(pcm[i]==0);
  ++outputs;return 0;
}
static int cancel(void *){return -ECANCELED;}
static void golden(const std::string &name) {
  FILE *f=std::fopen(name.c_str(),"rb");assert(f);
  assert(!std::fseek(f,0,SEEK_END));long bytes=std::ftell(f);
  assert(bytes>0 && bytes%320==0);expected_frames=bytes/320;
  assert(expected_frames<=200);expected_mel.resize(bytes/4);
  std::rewind(f);assert(std::fread(expected_mel.data(),1,bytes,f)==static_cast<std::size_t>(bytes));
  assert(!std::fclose(f));
}
int main(int argc,char **argv) {
  assert(argc==3);aipet::VoiceTtsEngine *engine=nullptr;
  assert(!aipet::voice_tts_engine_open(argv[1],&engine,0.0f) && engine);
  const char *texts[]={"你好","测试成功","打开灯","现在几点了","一加一等于二"};
  for(unsigned i=0;i<5;i++) {
    golden(std::string(argv[2])+"/case"+std::to_string(i)+".mel");
    assert(!aipet::voice_tts_engine_synthesize(engine,texts[i],sink,nullptr,now()+120000000,nullptr,nullptr));
    assert(opens==i+1 && closes==opens && outputs==i+1);
  }
  unsigned before=opens;
    // v214 intentionally pronounces OOV codepoints and spells Latin letters.
    // There is no captured acoustic golden for these NEW pronunciations.
    check_golden=false;new_text=true;
    assert(!aipet::voice_tts_engine_synthesize(engine,"🦄",sink,nullptr,now()+120000000,nullptr,nullptr));
    assert(!aipet::voice_tts_engine_synthesize(engine,"MiniCPM",sink,nullptr,now()+120000000,nullptr,nullptr));
    for(const char *text:{"Hello world. English TTS test.",
      "链路状态：好帧=397 坏帧=0 字节=3609 溢出=0 帧龄=0 ms 故障=0 失联=0 owner=0 A7Z=1 蓝牙=0 蓝牙初始化=1。",
      "电机脉冲状态：左位置=0 剩余=0 频率=0 标志=00；右位置=0 剩余=0 频率=0 标志=00；热跳闸=0 故障=0。不是编码器实测。"}) {
      assert(!aipet::voice_tts_engine_synthesize(engine,text,sink,nullptr,now()+120000000,nullptr,nullptr));
    }
    puts("PASS v221fix1: real CPU acoustic handles English spelling + complete board diagnostic text; NPU STUB, NOT audible English acceptance");
    assert(opens>before && opens==closes);before=opens;
    assert(aipet::voice_tts_engine_synthesize(engine,"\xf0\x80\x80\x80",sink,nullptr,now()+120000000,nullptr,nullptr)==-EILSEQ);
    assert(opens==before);check_golden=true;new_text=false;
  assert(aipet::voice_tts_engine_synthesize(engine,"你好",sink,nullptr,now()+120000000,cancel,nullptr)==-ECANCELED);
  assert(opens==before);
  assert(aipet::voice_tts_engine_synthesize(engine,"你好",sink,nullptr,now()-1,nullptr,nullptr)==-ETIMEDOUT);
  golden(std::string(argv[2])+"/case0.mel");before=outputs;
  bad_tail=true;
  assert(aipet::voice_tts_engine_synthesize(engine,"你好",sink,nullptr,now()+120000000,nullptr,nullptr)==-EILSEQ);
  assert(outputs==before && opens==closes);bad_tail=false;
  fail_run=-EIO;
  assert(aipet::voice_tts_engine_synthesize(engine,"你好",sink,nullptr,now()+120000000,nullptr,nullptr)==-EIO);
  assert(outputs==before && opens==closes);fail_run=0;
  fail_sink=-ECANCELED;
  assert(aipet::voice_tts_engine_synthesize(engine,"你好",sink,nullptr,now()+120000000,nullptr,nullptr)==-ECANCELED);
  assert(outputs==before && opens==closes);fail_sink=0;
  assert(!aipet::voice_tts_engine_synthesize(engine,"你好",sink,nullptr,now()+120000000,nullptr,nullptr));
  assert(outputs==before+1 && opens==closes);
  unsigned start_opens=opens;
  assert(!aipet::voice_tts_engine_session_begin(engine));
  assert(aipet::voice_tts_engine_session_begin(engine)==-EBUSY);
  fake_group=__real_getpid()+1;
  assert(aipet::voice_tts_engine_synthesize(engine,"你好",sink,nullptr,now()+120000000,nullptr,nullptr)==-EXDEV);
  assert(aipet::voice_tts_engine_session_end(engine)==-EPERM);
  fake_group=0;
  for(unsigned i=0;i<3;i++)
    assert(!aipet::voice_tts_engine_synthesize(engine,"你好",sink,nullptr,now()+120000000,nullptr,nullptr));
  assert(opens==start_opens+1 && opens==closes+1);
  assert(!aipet::voice_tts_engine_session_end(engine) && opens==closes);
  assert(!aipet::voice_tts_engine_session_end(engine));
  puts("PASS: same-command vocoder prepare once for three phrases; close at explicit session end");
  aipet::voice_tts_engine_close(engine);
  check_golden=false;engine=nullptr;
  assert(!aipet::voice_tts_engine_open(argv[1],&engine)); // actual upstream-default noise=1
  assert(!aipet::voice_tts_engine_synthesize(engine,"你好",sink,nullptr,now()+120000000,nullptr,nullptr));
  assert(opens==closes);aipet::voice_tts_engine_close(engine);
  puts("PASS: default noise=1 executes real CPU acoustic with finite mel; NOT a voice-quality/NPU test");
  std::printf("PASS: CPU mel -> exact padded native input; NPU STUB calls=%u; fd lifecycle/cancel/NaN tail/sink failure/reuse; NO audio saved, NO NPU execution\n",calls);
}
