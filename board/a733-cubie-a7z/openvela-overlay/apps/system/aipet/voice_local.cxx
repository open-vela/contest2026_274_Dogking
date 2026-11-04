/* SPDX-License-Identifier: Apache-2.0 */
#include "voice_local.hxx"
#include "pet_core.hxx"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <pthread.h>
namespace aipet {
namespace {
pthread_mutex_t owner=PTHREAD_MUTEX_INITIALIZER;
VoiceLocalProvider provider{};
long long now() {
  timespec t{};if(clock_gettime(CLOCK_MONOTONIC,&t)<0)return -(errno?errno:EIO);
  return t.tv_sec*1000000LL+t.tv_nsec/1000;
}
struct Lease { ~Lease(){pthread_mutex_unlock(&owner);} };
unsigned capabilities() {
  return (provider.asr_backend && provider.recognize?1u:0u) |
    (provider.acoustic_backend && provider.vocoder_backend && provider.synthesize?2u:0u);
}
bool valid(const char *text) { Reply reply;return text && parse_reply(text,reply); }
struct Sink {
  VoiceLocalPcmSink write;void *cookie;VoiceLocalCancel cancel;void *cancel_cookie;
  long long deadline;std::size_t bytes=0;int error=0;std::size_t limit=441000;
  int poll() {
    if(error)return error;
    long long at=now();if(at<0)return error=static_cast<int>(at);
    if(at>deadline)return error=-ETIMEDOUT;
    int rc=cancel?cancel(cancel_cookie):0;return error=rc?(rc<0?rc:-ECANCELED):0;
  }
};
int emit(void *cookie,const unsigned char *pcm,std::size_t bytes) {
  auto &s=*static_cast<Sink *>(cookie);
  if(s.poll())return s.error;
  if(!pcm || !bytes || bytes%2 || bytes>102400 || s.bytes>s.limit || bytes>s.limit-s.bytes)return s.error=-EMSGSIZE;
  int rc=s.write(s.cookie,pcm,bytes);
  if(rc)return s.error=rc<0?rc:-EIO;
  s.bytes+=bytes;return s.poll();
}
}
int voice_local_install(const VoiceLocalProvider *p)
{
  if(p && (!p->name || strnlen(p->name,65)>64 || !*p->name || p->asr_backend>2 ||
      p->acoustic_backend>2 || p->vocoder_backend>2 ||
      (!!p->asr_backend!=!!p->recognize) ||
      (!!p->synthesize!=bool(p->acoustic_backend && p->vocoder_backend))))return -EINVAL;
  if(p)for(const char *c=p->name;*c;++c)if(*c<32 || *c>126)return -EINVAL;
  if(pthread_mutex_trylock(&owner))return -EBUSY;
  Lease lease;provider=p?*p:VoiceLocalProvider{};return 0;
}
unsigned voice_local_capabilities()
{
  if(pthread_mutex_trylock(&owner))return 0;
  Lease lease;return capabilities();
}
void voice_local_status()
{
  if(pthread_mutex_trylock(&owner)){std::puts("local-voice: busy; no readiness change");return;}
  Lease lease;
  const char *names[]={"unavailable","CPU","NPU"};
  std::printf("local-voice: provider=%s asr=%s acoustic=%s vocoder=%s ready=%u; cloud-fallback=disabled\n",
    provider.name?provider.name:"not-installed",names[provider.asr_backend],
    names[provider.acoustic_backend],names[provider.vocoder_backend],capabilities()==3);
}
int voice_local_recognize(const unsigned char *pcm,std::size_t bytes,char *text,std::size_t cap,
                           VoiceLocalCancel cancel,void *cookie)
{
  if(!text || !cap)return -EINVAL;
  text[0]=0;
  if(!pcm || !bytes || bytes%2 || bytes>256000 || cap<2 || cap>4097)return -EINVAL;
  if(pthread_mutex_trylock(&owner))return -EBUSY;
  Lease lease;if(!(capabilities()&1))return -ENODEV;
  long long start=now();if(start<0)return static_cast<int>(start);
  Sink control{nullptr,nullptr,cancel,cookie,start+120000000LL};
  int rc=control.poll();
  /* Clear all bytes to prove a callback supplies a bounded C string. A backend
   * that writes no text becomes ENODATA, never an empty actuator command. */
  std::memset(text,0,cap);
  if(!rc)rc=provider.recognize(provider.cookie,pcm,bytes,text,cap,control.deadline,cancel,cookie);
  if(!rc)rc=control.poll();
  if(!rc && !std::memchr(text,0,cap))rc=-EILSEQ;
  if(!rc && !*text)rc=-ENODATA;
  if(!rc && !valid(text))rc=-EILSEQ;
  if(rc){std::memset(text,0,cap);return rc<0?rc:-EIO;}
  return 0;
}
static int speak(const char *text,VoiceLocalPcmSink sink,void *cookie,
                       VoiceLocalCancel cancel,void *cancel_cookie,std::size_t limit)
{
  if(!valid(text) || !sink)return -EINVAL;
  if(pthread_mutex_trylock(&owner))return -EBUSY;
  Lease lease;if(!(capabilities()&2))return -ENODEV;
  long long start=now();if(start<0)return static_cast<int>(start);
  Sink state{sink,cookie,cancel,cancel_cookie,start+120000000LL};
  state.limit=limit;
  int rc=state.poll();
  if(!rc)rc=provider.synthesize(provider.cookie,text,emit,&state,state.deadline,cancel,cancel_cookie);
  if(!rc)rc=state.poll();
  if(!rc && !state.bytes)rc=-ENODATA;
  return rc<0?rc:rc?-EIO:0;
}
int voice_local_speak(const char *t,VoiceLocalPcmSink s,void *c,VoiceLocalCancel stop,void *p)
{return speak(t,s,c,stop,p,441000);}
int voice_local_speak_stream(const char *t,VoiceLocalPcmSink s,void *c,VoiceLocalCancel stop,void *p)
{return speak(t,s,c,stop,p,2646000);}
}
