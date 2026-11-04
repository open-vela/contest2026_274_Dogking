/* SPDX-License-Identifier: Apache-2.0 */
#include "voice_local.hxx"
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
static bool fail_clock=false;
extern "C" int __real_clock_gettime(clockid_t,timespec *);
extern "C" int __wrap_clock_gettime(clockid_t id,timespec *t) {
  if(fail_clock){errno=EIO;return -1;}
  return __real_clock_gettime(id,t);
}
struct Context { unsigned asr=0,tts=0,played=0;int error=0,stop=0;bool invalid=false;unsigned seconds=0; };
static int cancel(void *p){return static_cast<Context *>(p)->stop;}
static int asr(void *p,const unsigned char *,size_t,char *out,size_t cap,long long,
               aipet::VoiceLocalCancel,void *)
{
  auto &c=*static_cast<Context *>(p);++c.asr;
  assert(aipet::voice_local_install(nullptr)==-EBUSY);
  if(c.invalid)std::memset(out,255,cap);else std::strcpy(out,"帮我打开灯");
  return c.error;
}
static int tts(void *p,const char *,aipet::VoiceLocalPcmSink sink,void *cookie,long long,
               aipet::VoiceLocalCancel,void *)
{
  auto &c=*static_cast<Context *>(p);++c.tts;
  if(c.seconds) {
    unsigned char block[4410]{};
    for(unsigned i=0;i<c.seconds*10;i++){int rc=sink(cookie,block,sizeof(block));if(rc)return rc;}
    return 0;
  }
  unsigned char pcm[4]{};int rc=sink(cookie,pcm,sizeof(pcm));
  if(rc)return rc;
  c.stop=c.invalid?-ECANCELED:0;
  /* Deliberately ignore this sink's failure to test sticky caller errors. */
  (void)sink(cookie,pcm,sizeof(pcm));
  return c.error;
}
static int output(void *p,const unsigned char *,size_t bytes)
{ auto &c=*static_cast<Context *>(p);c.played+=bytes;return 0; }
int main()
{
  Context c;unsigned char pcm[3200]{};char text[4097];
  assert(aipet::voice_local_capabilities()==0);
  assert(aipet::voice_local_recognize(pcm,sizeof(pcm),text,sizeof(text),cancel,&c)==-ENODEV);
  assert(aipet::voice_local_speak("你好",output,&c,cancel,&c)==-ENODEV);
  aipet::VoiceLocalProvider provider{"mock-CPU-and-NPU-contract",1,1,2,&c,asr,tts};
  assert(!aipet::voice_local_install(&provider) && aipet::voice_local_capabilities()==3);
  fail_clock=true;
  assert(aipet::voice_local_recognize(pcm,sizeof(pcm),text,sizeof(text),cancel,&c)==-EIO && c.asr==0 && !*text);
  assert(aipet::voice_local_speak("你好",output,&c,cancel,&c)==-EIO && c.tts==0);
  fail_clock=false;
  assert(!aipet::voice_local_recognize(pcm,sizeof(pcm),text,sizeof(text),cancel,&c));
  assert(!std::strcmp(text,"帮我打开灯") && c.asr==1);
  c.error=-EIO;
  assert(aipet::voice_local_recognize(pcm,sizeof(pcm),text,sizeof(text),cancel,&c)==-EIO && !*text);
  c.error=0;c.invalid=true;
  assert(aipet::voice_local_recognize(pcm,sizeof(pcm),text,sizeof(text),cancel,&c)==-EILSEQ && !*text);
  c.invalid=false;c.stop=-ECANCELED;
  assert(aipet::voice_local_recognize(pcm,sizeof(pcm),text,sizeof(text),cancel,&c)==-ECANCELED && c.asr==3);
  c.stop=0;assert(!aipet::voice_local_speak("你好",output,&c,cancel,&c));
  assert(c.tts==1 && c.played==8);
  c.invalid=true;assert(aipet::voice_local_speak("你好",output,&c,cancel,&c)==-ECANCELED);
  assert(c.tts==2 && c.played==12);
  c.stop=0;c.invalid=false;c.error=-ETIMEDOUT;
  assert(aipet::voice_local_speak("你好",output,&c,cancel,&c)==-ETIMEDOUT);
  c.error=0;c.seconds=11;c.played=0;
  assert(aipet::voice_local_speak("旧缓冲接口",output,&c,cancel,&c)==-EMSGSIZE&&c.played==441000);
  c.played=0;
  assert(!aipet::voice_local_speak_stream("流式接口",output,&c,cancel,&c)&&c.played==485100);
  c.seconds=61;c.played=0;
  assert(aipet::voice_local_speak_stream("超限仍报错",output,&c,cancel,&c)==-EMSGSIZE&&c.played==2646000);
  puts("PASS v221fix1: buffered10s retained, streaming11s accepted,60s hard bound enforced");
  assert(!aipet::voice_local_install(nullptr) && !aipet::voice_local_capabilities());
  std::puts("Local provider qualification/owner/cancel/failure/publication tests PASS (mocks only)");
}
