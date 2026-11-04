/* SPDX-License-Identifier: Apache-2.0 */
#include "audio_app.hxx"
#include "voice_npu_check.hxx"
#include "voice_local.hxx"
#include "voice_general_provider.hxx"
#include "local_llm.hxx"
#include "pet_routes.hxx"
#include "voice/voice_asr.h"
#include "voice/voice_tts.h"
#include <system/a733_pcm.h>
#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>
#include <cstdarg>
#ifdef AIPET_TEST_RESIDENT
#include <atomic>
#include <spawn.h>
static pthread_t resident_thread;
static std::atomic<unsigned> resident_waits{0},resident_spawns{0};
static unsigned resident_stdio=0;
static int resident_result=-999;
static thread_local bool resident_foreign=false;
extern "C" pid_t __real_getpid();
extern "C" pid_t __wrap_getpid(){return __real_getpid()+(resident_foreign?1:0);}
extern "C" int __real_posix_spawn_file_actions_addopen(posix_spawn_file_actions_t *,int,const char *,int,mode_t);
extern "C" int __wrap_posix_spawn_file_actions_addopen(posix_spawn_file_actions_t *files,int fd,const char *name,int flags,mode_t mode){
  assert(fd==int(resident_stdio%3));
  assert(!std::strcmp(name,fd?"/dev/a7zconsole":"/dev/null"));
  assert(flags==(fd?O_WRONLY:O_RDONLY));++resident_stdio;
  return __real_posix_spawn_file_actions_addopen(files,fd,name,flags,mode);
}
extern "C" int __wrap_posix_spawn(pid_t *pid,const char *name,const posix_spawn_file_actions_t *files,
    const posix_spawnattr_t *,char *const args[],char *const []) {
  assert(!std::strcmp(name,"aipet") && !std::strcmp(args[1],"--voice-worker") && !args[2]);
  assert(files && resident_stdio==3*(resident_spawns.load()+1));
  ++resident_spawns;*pid=getpid();
  return pthread_create(&resident_thread,nullptr,[](void *)->void *{
    resident_result=aipet::audio_voice_service_main();return nullptr;
  },nullptr);
}
#endif
/* Host-only legacy routing harness. This test does not link ORT/NPU; reject
 * explicit general-provider requests rather than fabricate recognition or
 * synthesis. Real general-provider execution has separate model tests.
 */
namespace aipet {
static bool general_selected=false,general_empty=false;
static unsigned general_opens=0,general_listens=0,general_closes=0,warmups=0;
#ifndef AIPET_MCU_INTEGRATION
static unsigned offline_replies=0;
#endif
static int warmup_result=1;
static int preflight_result=0;
int local_llm_preflight(){return preflight_result;}
struct VoiceGeneralSession {};
int voice_general_tts_selected() { return general_selected?1:0; }
int voice_general_configure(const char *) { return -ENOTSUP; }
int voice_general_configure_full(const char *, const char *) { return -ENOTSUP; }
int voice_general_live(unsigned, unsigned, unsigned, char *, std::size_t,
                       VoiceLocalCancel, void *) { return -ENOTSUP; }
int voice_general_session_open(VoiceGeneralSession **out,VoiceLocalCancel,void *) {
  *out=nullptr;if(!general_selected)return -ENOTSUP;*out=new VoiceGeneralSession;++general_opens;return 0;
}
int voice_general_session_listen(VoiceGeneralSession *s,unsigned,unsigned,unsigned,char *out,std::size_t,
                                VoiceLocalCancel,void *) {
  assert(s);++general_listens;std::strcpy(out,general_empty?"":"现在几点了");return 0;
}
int voice_general_session_close(VoiceGeneralSession *s) {if(s){delete s;++general_closes;}return 0;}
#ifdef AIPET_TEST_RESIDENT
int audio_voice_service_phase_for_test();
int voice_general_session_wait(VoiceGeneralSession *s,unsigned gain,unsigned floor,char *out,std::size_t,
    VoiceLocalCancel cancel,void *cc,LiveVoiceState state,void *sc) {
  assert(s&&gain==5&&floor==400);unsigned turn=++resident_waits;
  state(sc,1);out[0]=0;
  if(turn==1)return 0; // empty recognition is not a conversational turn
  if(turn==2)return -EMSGSIZE; // discarded, not sent to LLM
  if(turn<=4){state(sc,2);state(sc,3);state(sc,1);state(sc,2);
    assert(audio_voice_service_phase_for_test()==5); // late consumer cannot turn MIC back ON
    std::strcpy(out,"你好");return 0;}
  for(unsigned i=0;i<10000;i++){int rc=cancel(cc);if(rc)return rc;usleep(1000);}
  return -ETIMEDOUT;
}
#endif
void voice_general_session_retain(VoiceGeneralSession *) {}
int local_llm_control(const char *,const char *) {++warmups;return warmup_result;} // no CPU weights in this routing harness
}
/* Host-only path adapter; production still restricts WAV entry to /data/. */
extern "C" int __real_open(const char *,int,...);
extern "C" int __wrap_open(const char *path,int flags,...)
{
  int mode=0;if(flags&O_CREAT){va_list args;va_start(args,flags);mode=va_arg(args,int);va_end(args);}
  const char *actual=!std::strcmp(path,"/data/kws-fixture.wav")?AIPET_AUDIO_RECORD:
    !std::strcmp(path,"/data/audit-export.f32")?AIPET_AUDIO_RECORD ".f32":path;
  return __real_open(actual,flags,mode);
}

static int phase=2, asr_error, tts_error, capture_error;
static unsigned captures, uploads, syntheses, plays, routes, uart_calls;
static unsigned expected_gain=5, expected_volume=50;
static unsigned expected_rate=16000;
static size_t expected_play_bytes=4;
static size_t expected_asr_bytes=3200;
static std::size_t tts_size=4;
static bool bad_asr, reenter;
static const char *asr_text="你好";
static std::string last_spoken;
static unsigned tts_pause_us;
static bool interrupt_capture, interrupt_tts;
static unsigned npuref_calls;
static int npuref_error;
static unsigned npukws_calls;
static unsigned kws_memory_calls,kws_input_calls;
static bool stream_supported=false;
static unsigned stream_plays=0;
static int kws_memory_error;
extern "C" void aipet_agent_status(int *p,int *pid,int *error)
{ *p=phase; *pid=1; *error=0; }
extern "C" int aipet_voice_setup(void) { return 0; }
extern "C" void aipet_wipe(void *p,size_t n)
{ volatile unsigned char *b=static_cast<volatile unsigned char *>(p); while(n--) *b++=0; }
extern "C" int a733_pcm_available(void) { return 0; }
extern "C" int a733_pcm_capture(unsigned ms,unsigned gain,unsigned char *pcm,
                                size_t capacity,size_t *bytes)
{
  captures++; assert(gain==expected_gain && capacity==ms*96u);
  if(interrupt_capture) assert(pthread_kill(pthread_self(),SIGINT)==0);
  *bytes=0;
  if (capture_error) return capture_error;
  for(size_t i=0;i<capacity/2;i++) { pcm[i*2]=0x00; pcm[i*2+1]=0x10; }
  *bytes=capacity; return 0;
}
extern "C" int a733_pcm_play(const unsigned char *pcm,size_t bytes,unsigned rate,
                             unsigned channels,unsigned volume)
{
  assert(pcm && bytes==expected_play_bytes && rate==expected_rate && channels==1 && volume==expected_volume);
  plays++; return 0;
}
extern "C" int a733_pcm_play_file(const char *path,unsigned volume)
{ assert(!std::strcmp(path,"/data/test.wav") && volume==expected_volume); plays++; return 0; }
extern "C" int a733_pcm_stream(a733_pcm_reader reader,void *cookie,unsigned volume,uint32_t *frames)
{
  *frames=0;if(!stream_supported)return -ENOSYS;
  assert(volume==expected_volume);unsigned char data[960];uint32_t bytes;
  for(;;){int rc=reader(cookie,data,sizeof(data),&bytes);if(rc)return rc;if(!bytes)break;
    assert(bytes<=960&&bytes%2==0);*frames+=bytes/2;}
  ++stream_plays;return 0;
}
extern "C" int a733_pcm_capture_stream(unsigned ms,unsigned gain,a733_pcm_capture_sink sink,
                                       void *cookie,uint32_t *frames,unsigned *ended)
{
  assert(ms>=100&&ms<=8000&&gain==expected_gain);
  ++captures;*frames=*ended=0;
  if(capture_error)return capture_error;
  unsigned char pcm[960]{};
  for(unsigned at=0;at<ms*48u;){
    unsigned n=ms*48u-at;if(n>480)n=480;
    for(unsigned i=0;i<n;i++){pcm[2*i]=0x00;pcm[2*i+1]=0x10;}
    int rc=sink(cookie,pcm,n*2);if(rc<0)return rc;at+=n;
    if(rc==1){*ended=1;*frames=at;return 0;}
    usleep(1000);
  }
  *frames=ms*48u;return 0;
}
extern "C" const char *voice_asr_get_backend(void) { return "volcengine"; }
/* Audio integration seam mocks; real provider/crypto/protocol tests are in
 * test-cloud-voice-v218.py. These do not pretend to execute a cloud API. */
extern "C" int aipet_voice_provider_setup(const char *){return 0;}
extern "C" int aipet_voice_provider_select(const char *,const char *){return 0;}
extern "C" void aipet_cloud_status(void){}
extern "C" int aipet_cloud_tts(const char *text,unsigned char *pcm,size_t cap,size_t *n){return voice_tts_speak(text,pcm,cap,n);}
extern "C" int aipet_cloud_asr(const unsigned char *p,size_t n,char *text,size_t cap){return voice_asr_recognize(p,n,text,cap);}
extern "C" int aipet_cloud_asr_cancel(const unsigned char *p,size_t n,char *text,size_t cap,int (*cancel)(void *),void *cookie){int r=cancel?cancel(cookie):0;return r?r:aipet_cloud_asr(p,n,text,cap);}
extern "C" int aipet_cloud_tts_cancel(const char *text,unsigned char *p,size_t cap,size_t *n,int (*cancel)(void *),void *cookie){int r=cancel?cancel(cookie):0;return r?r:aipet_cloud_tts(text,p,cap,n);}
extern "C" const char *voice_tts_get_backend(void) { return "volcengine"; }
extern "C" int voice_asr_recognize(const unsigned char *pcm,size_t bytes,
                                   char *text,size_t cap)
{
  assert(pcm && bytes==expected_asr_bytes && cap==4097); uploads++;
  if(asr_error) return asr_error;
  std::strcpy(text,bad_asr ? "\xff" : asr_text); return 0;
}
extern "C" int voice_tts_speak(const char *text,unsigned char *pcm,size_t cap,size_t *bytes)
{
  assert(text && pcm && cap==320000); syntheses++;
  last_spoken=text;
  if(tts_pause_us)usleep(tts_pause_us);
  if(interrupt_tts) assert(pthread_kill(pthread_self(),SIGINT)==0);
  if(reenter) assert(aipet::audio_i2s_speak("nested")==-EBUSY);
  if(tts_error) return tts_error;
  pcm[0]=1; pcm[1]=2; pcm[2]=3; pcm[3]=4; *bytes=tts_size; return 0;
}
namespace aipet {
/* Dispatch seam only. The real helper/client has separate ASan/UBSan tests. */
int voice_npu_kws(int argc,char **argv)
{
  assert(argc==2 && !std::strcmp(argv[0],"/data/npu/voice") && !std::strcmp(argv[1],"/data/test.f32"));
  ++npukws_calls;return 0;
}
int voice_npu_kws_input(int argc,char **argv)
{ assert(argc==2 && !std::strcmp(argv[0],"/data/npu/voice"));++kws_input_calls;return 0; }
int voice_npu_kws_memory(const char *base,const float *features,unsigned frames)
{
  assert(!std::strcmp(base,"/data/npu/voice") && features && frames>=29 && frames<=1000);
  for(unsigned i=0;i<frames*80;++i)assert(std::isfinite(features[i]));
  ++kws_memory_calls;return kws_memory_error;
}
int voice_npu_check(int argc,char **argv)
{
  assert(argc==3 && !std::strcmp(argv[0],"joiner") &&
         !std::strcmp(argv[1],"/data/model.a7tv") && !std::strcmp(argv[2],"/data/input"));
  ++npuref_calls; return npuref_error;
}
#ifndef AIPET_MCU_INTEGRATION
int routed_offline_stream_ask(const char *){++offline_replies;return general_selected?0:-ENODEV;}
int routed_offline_ask(const char *text) {
  assert(!std::strcmp(text,"帮我打开灯"));++routes;
  return audio_i2s_local_speak("回答");
}
int routed_ask(const char *text,bool inspect,bool force)
{
  assert(!std::strcmp(text,"你好") && !inspect && !force); routes++;
  /* Prove the listen path releases its lock before routing/presentation. */
  return audio_i2s_selected() ? (audio_i2s_speak("回答")==0 ? 0 : 1) : 0;
}
int uart_tts_control(const char *command,const char *text)
{ assert(!std::strcmp(command,"test") && text); uart_calls++; return 0; }
#endif
}
static int command(std::initializer_list<const char *> arguments)
{
  std::vector<char *> argv;
  for(auto a:arguments) argv.push_back(const_cast<char *>(a));
  return aipet::audio_control(argv.size(),argv.data());
}
static double decimated_tone(double frequency)
{
  constexpr unsigned frames=48000;
  std::vector<unsigned char> in(frames*2),out(frames*2/3);
  for(unsigned i=0;i<frames;i++) {
    auto sample=static_cast<int16_t>(10000*std::sin(2*3.141592653589793*frequency*i/48000));
    uint16_t v=static_cast<uint16_t>(sample); in[i*2]=v&255; in[i*2+1]=v>>8;
  }
  size_t used=0;
  assert(aipet::audio_decimate(in.data(),in.size(),out.data(),out.size(),&used)==0);
  assert(used==32000);
  double sum=0;
  for(unsigned i=100;i<15900;i++) {
    int raw=out[i*2] | unsigned(out[i*2+1])<<8;
    int sample=raw>=32768 ? raw-65536 : raw; sum+=double(sample)*sample;
  }
  return std::sqrt(sum/15800);
}
int main()
{
#ifdef AIPET_TEST_RESIDENT
  using namespace aipet;
  assert(audio_voice_service_main()==1); // cannot invoke hidden worker directly
  assert(!command({"output","i2s"}));assert(!command({"volume","50"}));assert(!command({"gain","5"}));
  general_selected=true;warmup_result=0;
  assert(!command({"service","start"}));
  for(unsigned i=0;i<10000 && resident_waits.load()<5;i++)usleep(1000);
  assert(resident_waits==5&&audio_voice_service_active());
  assert(command({"service","start"})==1 && resident_spawns==1);
  assert(command({"output","off"})==1 && audio_i2s_selected());
  assert(command({"record","100"})==1 && captures==0);
  resident_foreign=true; // public API callers also cannot steal the idle gap
  assert(audio_i2s_speak("foreign")==-EBUSY);
  assert(audio_i2s_local_speak("foreign")==-EBUSY);
  assert(audio_i2s_local_stream_speak("foreign")==-EBUSY);
  resident_foreign=false;
  assert(!command({"service","status"}));assert(!command({"service","stop"}));
  assert(!pthread_join(resident_thread,nullptr));
  assert(!resident_result&&!audio_voice_service_active());
  assert(general_opens==1&&general_closes==1&&warmups==1&&offline_replies==2);
  // Restart is explicit, fails preload before recording and cleans up.
  warmup_result=1;
  assert(!command({"service","start"}));assert(!pthread_join(resident_thread,nullptr));
  assert(resident_result==1&&!audio_voice_service_active()&&resident_waits==5);
  assert(general_opens==general_closes&&resident_spawns==2);
  preflight_result=-ENOENT;
  assert(command({"service","start"})==1 && !audio_voice_service_active() && resident_spawns==2);
  preflight_result=0;
  puts("PASS v214 actual service lifecycle: independent builtin spawn entry, single preload/session, empty/overlong ignored, two replies, config exclusion, idle cooperative stop, restart/preflight failure; HOST MOCKS NOT NUTTX task acceptance");
  return 0;
#endif
  assert(command({"status"})==0 && captures==0 && uploads==0 && syntheses==0);
  assert(aipet::audio_i2s_selected() && !aipet::audio_muted());
  assert(command({"record","100"})==0 && captures==1 && uploads==0);
  FILE *wav=std::fopen(AIPET_AUDIO_RECORD,"rb"); assert(wav);
  unsigned char header[44]; assert(std::fread(header,1,44,wav)==44);
  assert(!std::memcmp(header,"RIFF",4) && header[24]==0x80 && header[25]==0xbb);
  assert(!std::fseek(wav,0,SEEK_END) && std::ftell(wav)==9644); std::fclose(wav);
  for(auto value:{"-1","+1","101","4294967296","50x"," 50"})
    assert(command({"volume",value})==1);
  assert(command({"gain","0"})==1 && command({"gain","17"})==1);
  assert(command({"output","i2s"})==0 && aipet::audio_i2s_selected());
  assert(command({"volume","70"})==0); expected_volume=70;
  assert(command({"gain","6"})==0); expected_gain=6;
  assert(command({"status"})==0); /* reload persisted fields, not global-only state */
  assert(command({"play","/data/test.wav"})==0);
  assert(command({"asr","100"})==0 && uploads==1 && routes==0);
  interrupt_capture=true;
  assert(command({"asr","100"})==1 && uploads==1);
  interrupt_capture=false;
  assert(command({"listen","100"})==0 && uploads==2 && routes==1 && syntheses==1);
  unsigned previous=plays;
  tts_error=-ENOENT;
  assert(command({"speak","测试"})==1 && plays==previous);
  tts_error=0; tts_size=320002;
  assert(command({"speak","测试"})==1 && plays==previous);
  tts_size=3; assert(command({"speak","测试"})==1 && plays==previous);
  tts_size=4; reenter=true;
  assert(command({"speak","测试"})==0 && plays==previous+1); reenter=false;
  previous=plays; interrupt_tts=true;
  assert(command({"speak","取消"})==1 && plays==previous);
  interrupt_tts=false;
  assert(command({"speak","恢复"})==0 && plays==previous+1);
  previous=syntheses;
  assert(command({"volume","0"})==0);
  assert(command({"speak","静音"})==0 && syntheses==previous);
  assert(command({"volume","70"})==0);
  bad_asr=true; previous=routes;
  assert(command({"listen","100"})==1 && routes==previous); bad_asr=false;
  capture_error=-EOVERFLOW; previous=uploads;
  assert(command({"listen","100"})==1 && uploads==previous); capture_error=0;
  phase=0; previous=captures;
  assert(command({"listen","100"})==1 && captures==previous);
  phase=2;
  assert(command({"output","off"})==0 && aipet::audio_muted());
  previous=syntheses;
  assert(command({"speak","测试"})==1 && syntheses==previous);
  assert(command({"output","uart"})==1);
  assert(command({"speak","测试"})==1 && uart_calls==0);
  assert(command({"record","99"})==1 && command({"record","10001"})==1);
  double pass=decimated_tone(1000), stop=decimated_tone(12000);
  assert(pass>6900 && pass<7200 && stop<pass*0.01); /* >40 dB rejection */
  size_t used=123; unsigned char in[6]={0},out[2];
  assert(aipet::audio_decimate(in,5,out,2,&used)==-EINVAL && used==0);
  assert(aipet::audio_decimate(in,6,out,1,&used)==-EINVAL);
  FILE *blocked=std::fopen(AIPET_AUDIO_DIR "/config/audio.conf.new","wb");
  assert(blocked); std::fputs("interrupted-save",blocked); std::fclose(blocked);
  assert(command({"volume","20"})==1); /* no overwrite of unknown temp */
  assert(!unlink(AIPET_AUDIO_DIR "/config/audio.conf.new"));
  assert(command({"play","/data/test.wav"})==0); /* still saved 70%, not 20% */
  /* Existing 48k recording is rejected by the explicit Matcha 22.05k adapter,
   * with no playback or cloud request. Then patch a valid 0.1s PCM fixture. */
  previous=plays; unsigned before_uploads=uploads,before_synth=syntheses;
  assert(command({"play22k",AIPET_AUDIO_RECORD})==1 && plays==previous);
  unsigned char matcha[4454]={0};
  std::memcpy(matcha,"RIFF",4); matcha[4]=0x5e; matcha[5]=0x11;
  std::memcpy(matcha+8,"WAVEfmt ",8); matcha[16]=16;
  matcha[20]=1; matcha[22]=1; matcha[24]=0x22; matcha[25]=0x56;
  matcha[28]=0x44; matcha[29]=0xac; matcha[32]=2; matcha[34]=16;
  std::memcpy(matcha+36,"data",4); matcha[40]=0x3a; matcha[41]=0x11;
  wav=std::fopen(AIPET_AUDIO_RECORD,"wb"); assert(wav);
  assert(std::fwrite(matcha,1,sizeof(matcha),wav)==sizeof(matcha)); std::fclose(wav);
  expected_rate=48000; expected_play_bytes=9600;
  assert(command({"play22k",AIPET_AUDIO_RECORD})==0 && plays==previous+1);
  assert(uploads==before_uploads && syntheses==before_synth);
  /* Local frontend works before Agent/cloud initialization. No network, no
   * playback and no overwriting the explicitly exported recording. */
  phase=0;
  assert(command({"features","100"})==0 && plays==previous+1);
  assert(uploads==before_uploads && syntheses==before_synth);
  wav=std::fopen(AIPET_AUDIO_RECORD,"rb"); assert(wav);
  assert(!std::fseek(wav,0,SEEK_END) && std::ftell(wav)==sizeof(matcha));
  std::fclose(wav);
  interrupt_capture=true;
  assert(command({"features","100"})==1 && uploads==before_uploads);
  interrupt_capture=false;
  assert(command({"features","100"})==0 && uploads==before_uploads);
  assert(command({"features","99"})==1 && command({"features","10001"})==1);
  phase=2;
  /* Explicit local buffered KWS: capture stops before frontend/NPU; no file,
   * upload, Agent readiness, TTS, routing or speech. */
  phase=0;
  unsigned kw_uploads=uploads,kw_routes=routes,kw_plays=plays,kw_synth=syntheses;
  assert(command({"kwsmic","/data/npu/voice","500"})==0 && kws_memory_calls==1);
  assert(command({"kwsmic","/data/npu/voice","499"})==1 && kws_memory_calls==1);
  assert(command({"kwsmic","/data/npu/voice","10001"})==1 && kws_memory_calls==1);
  capture_error=-EOVERFLOW;
  assert(command({"kwsmic","/data/npu/voice","500"})==1 && kws_memory_calls==1);capture_error=0;
  interrupt_capture=true;
  assert(command({"kwsmic","/data/npu/voice","500"})==1 && kws_memory_calls==1);interrupt_capture=false;
  kws_memory_error=-EIO;
  assert(command({"kwsmic","/data/npu/voice","500"})==1 && kws_memory_calls==2);kws_memory_error=0;
  assert(command({"kwsmic","/data/npu/voice","500"})==0 && kws_memory_calls==3);
  assert(uploads==kw_uploads && routes==kw_routes && plays==kw_plays && syntheses==kw_synth);
  assert(command({"kwsinput","/data/npu/voice","/data/test.f32"})==0 && kws_input_calls==1);
  wav=std::fopen(AIPET_AUDIO_RECORD,"rb");assert(wav);
  assert(!std::fseek(wav,0,SEEK_END) && std::ftell(wav)==sizeof(matcha));std::fclose(wav);
  assert(command({"record","500"})==0);
  unsigned audit_captures=captures;
  assert(command({"wavinfo","/data/kws-fixture.wav"})==0);
  assert(command({"fbankexport","/data/kws-fixture.wav","/data/audit-export.f32"})==0);
  assert(command({"fbankcheck","/data/kws-fixture.wav","/data/audit-export.f32"})==0);
  assert(command({"fbankexport","/data/kws-fixture.wav","/data/audit-export.f32"})==1);
  assert(command({"wavinfo","/data/../kws-fixture.wav"})==1 && captures==audit_captures);
  assert(command({"kwswav","/data/npu/voice","/data/kws-fixture.wav"})==0 && kws_memory_calls==4);
  assert(command({"kwswav","/data/npu/voice","relative.wav"})==1 && kws_memory_calls==4);
  wav=std::fopen(AIPET_AUDIO_RECORD,"r+b");assert(wav);assert(!std::fseek(wav,22,SEEK_SET));assert(std::fputc(2,wav)==2);std::fclose(wav);
  assert(command({"kwswav","/data/npu/voice","/data/kws-fixture.wav"})==1 && kws_memory_calls==4);
  assert(uploads==kw_uploads && routes==kw_routes && plays==kw_plays && syntheses==kw_synth);
  /* New explicit offline commands never borrow the cloud providers, including
   * when credentials exist. Missing target engines refuse BEFORE recording. */
  assert(command({"output","i2s"})==0);
  assert(command({"local-status"})==0);
  previous=captures;before_uploads=uploads;before_synth=syntheses;
  assert(command({"local-asr","100"})==1 && captures==previous);
  assert(command({"local-listen","100"})==1 && captures==previous);
  assert(command({"local-speak","你好"})==1);
  assert(uploads==before_uploads && syntheses==before_synth);
  auto local_asr=[](void *,const unsigned char *,size_t,char *text,size_t,long long,
                    aipet::VoiceLocalCancel,void *){std::strcpy(text,"帮我打开灯");return 0;};
  auto local_tts=[](void *,const char *,aipet::VoiceLocalPcmSink sink,void *cookie,long long,
                    aipet::VoiceLocalCancel,void *){unsigned char pcm[882]{};return sink(cookie,pcm,sizeof(pcm));};
  aipet::VoiceLocalProvider provider{"mock-test-not-hardware",1,1,2,nullptr,local_asr,local_tts};
  assert(!aipet::voice_local_install(&provider));
  phase=0;expected_play_bytes=1920;expected_rate=48000;
  assert(command({"local-asr","100"})==0 && captures==previous+1);
  assert(command({"local-listen","100"})==0 && captures==previous+2);
  assert(command({"local-speak","你好"})==0);
  assert(uploads==before_uploads && syntheses==before_synth);
  unsigned local_plays=plays;
  provider.synthesize=[](void *,const char *,aipet::VoiceLocalPcmSink sink,void *cookie,long long,
                         aipet::VoiceLocalCancel,void *) {
    unsigned char partial[882]{};assert(!sink(cookie,partial,sizeof(partial)));return -EIO;
  };
  assert(!aipet::voice_local_install(&provider));
  assert(command({"local-speak","失败"})==1 && plays==local_plays); /* no partial playback */
  assert(!aipet::voice_local_install(nullptr));
  assert(uploads==before_uploads && syntheses==before_synth);
  phase=2;
  unsigned live_uploads=uploads,live_synth=syntheses,live_routes=routes;
  assert(command({"stream-features","100"})==0);
  assert(command({"stream-features","101"})==0);
  assert(command({"stream-features","99"})==1);
  assert(command({"stream-features","100","400"})==1);
  capture_error=-EOVERFLOW;assert(command({"stream-features","100"})==1);capture_error=0;
  assert(command({"stream-features","100"})==0);
  stream_supported=true;
  assert(command({"stream-echo","100"})==0&&stream_plays==1);
  assert(command({"output","off"})==0);
  previous=captures;assert(command({"stream-echo","100"})==1&&captures==previous);
  assert(command({"output","i2s"})==0);
  stream_supported=false;
  assert(uploads==live_uploads&&syntheses==live_synth&&routes==live_routes);
#ifndef AIPET_MCU_INTEGRATION
  aipet::general_selected=true;
  unsigned prepared=aipet::general_opens;
  assert(command({"offline","0"})==1);
  assert(command({"offline","2","99"})==1);
  assert(command({"offline","2","100","99"})==1);
  assert(command({"offline","2","100","0"})==1); // LLM preflight fails BEFORE microphone/session
  assert(aipet::general_opens==prepared);
  aipet::warmup_result=0;
  assert(command({"offline","3","100","0"})==0);
  assert(aipet::general_opens==prepared+1&&aipet::general_listens==3&&aipet::offline_replies==3);
  assert(aipet::general_opens==aipet::general_closes);
  aipet::general_empty=true;
  assert(command({"offline","2","100","0"})==0&&aipet::offline_replies==3);
  assert(aipet::general_opens==aipet::general_closes);
  aipet::general_empty=false;aipet::general_selected=false;
  assert(uploads==live_uploads&&syntheses==live_synth); // no implicit cloud ASR/TTS
#endif
  FILE *damaged=std::fopen(AIPET_AUDIO_DIR "/config/audio.conf","wb");
  assert(damaged); std::fputs("damaged-config",damaged); std::fclose(damaged);
  assert(command({"status"})==1 && aipet::audio_muted() && !aipet::audio_i2s_selected());
  wav=std::fopen(AIPET_AUDIO_RECORD,"r+b");assert(wav);assert(!std::fseek(wav,22,SEEK_SET));assert(std::fputc(1,wav)==1);std::fclose(wav);
  assert(command({"wavinfo","/data/kws-fixture.wav"})==0); /* independent of damaged config */
  /* NPU diagnostics must not depend on cloud readiness or valid audio config,
   * and must not start capture/upload/speech. This is only dispatch testing. */
  phase=0;
  const unsigned capture_before=captures,upload_before=uploads,synth_before=syntheses,play_before=plays;
  assert(command({"npuref","joiner","/data/model.a7tv","/data/input"})==0 && npuref_calls==1);
  assert(command({"npukws","/data/npu/voice","/data/test.f32"})==0 && npukws_calls==1);
  npuref_error=-ENOTTY;
  assert(command({"npuref","joiner","/data/model.a7tv","/data/input"})==1 && npuref_calls==2);
  assert(captures==capture_before && uploads==upload_before && syntheses==synth_before && plays==play_before);
  std::printf("PASS: app config persistence, local record/no upload, FIR pass=%.1f stop=%.1f, ASR->route->PCM, retired UART/off selection, validation/errors, no partial TTS, lock release/reentry\n",pass,stop);
  return 0;
}
