/* SPDX-License-Identifier: Apache-2.0 */
// Actual queue/resampler/ownership code, HOST mock provider/device, NOT sound.
#include "voice_phrase_pipe.hxx"
#include "voice_play_stream.hxx"
#include <system/a733_pcm.h>
#include <atomic>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <unistd.h>
using namespace aipet;
static std::atomic<unsigned> synths{0}, pulls{0}, feeds{0};
static unsigned produced_while_playing=0;
extern "C" int a733_pcm_stream(a733_pcm_reader read,void *p,unsigned volume,uint32_t *frames) {
  assert(volume==50);*frames=0;unsigned char pcm[960];
  for(;;){unsigned n=0;int rc=read(p,pcm,sizeof(pcm),&n);if(rc)return rc;
    if(!n)return *frames?0:-ENODATA;
    *frames+=n/2;pulls+=n/2;usleep(1000);}
}
static int synth(void *,const char *text,VoiceLocalPcmSink sink,void *p,long long,
    VoiceLocalCancel cancel,void *cookie) {
  assert(text && *text);unsigned count=++synths;
  if(count==2 && pulls>0 && pulls<48000)produced_while_playing++;
  unsigned char pcm[4410];memset(pcm,1,sizeof(pcm));
  for(unsigned i=0;i<10;i++){int rc=cancel(cookie);if(rc)return rc;
    rc=sink(p,pcm,sizeof(pcm));if(rc)return rc;usleep(1000);}
  return 0;
}
static int feed(void *,VoiceTextNext next,void *source,VoiceLocalCancel cancel,void *cookie) {
  ++feeds;VoicePlayMetrics m{};return voice_play_feed(next,source,50,cancel,cookie,&m);
}
int main() {
  VoiceLocalProvider provider{"HOST-MOCK-NOT-NPU-OR-AUDIO",0,1,1,nullptr,nullptr,synth};
  assert(!voice_local_install(&provider));VoicePhrasePipe *p=nullptr;VoicePhraseMetrics m{};
  assert(!voice_phrase_open_feed(feed,nullptr,&p));
  const char *text="第一句完整的话。第二句包含MiniCPM这个名字。第三句也不会丢掉。";
  assert(!voice_phrase_accept(p,text,strlen(text)));
  assert(!voice_phrase_finish(p,0,&m));voice_phrase_close(p);
  assert(feeds==1 && synths==3 && m.queued==3 && m.spoken==3 && pulls==3*48000);
  assert(produced_while_playing==1);
  // Critical: the next-text callback must not hide cancellation while idle.
  assert(!voice_phrase_open_feed(feed,nullptr,&p));usleep(20000);
  assert(voice_phrase_finish(p,-ECANCELED,&m)==-ECANCELED);voice_phrase_close(p);
  assert(!voice_local_install(nullptr));
  puts("PASS: real pipe + shared PCM queue; next clause synthesized during playback; all frames, idle cancellation, no NPU/hardware/audio acceptance");
}
