/* SPDX-License-Identifier: Apache-2.0 */
#include "audio_capture_live.hxx"
#include <system/a733_pcm.h>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <memory>
#include <vector>
#include <pthread.h>
#include <unistd.h>
using namespace aipet;
namespace aipet {int audio_decimate(const unsigned char *,std::size_t,unsigned char *,std::size_t,std::size_t *);}
static unsigned mode=0,cancelled=0;
static std::vector<unsigned char> wire,accepted;
static bool ready=false,speech=false,join_fail=false;
static pthread_t kept{};
extern "C" int __real_pthread_join(pthread_t,void **);
extern "C" int __wrap_pthread_join(pthread_t t,void **r){
  if(join_fail){join_fail=false;kept=t;return EINVAL;}return __real_pthread_join(t,r);
}
extern "C" void __lsan_disable();extern "C" void __lsan_enable();
static void block(unsigned char *p,int amplitude,int offset=0){
  for(unsigned i=0;i<480;i++){uint16_t v=int16_t((i%2?amplitude:-amplitude)+offset);p[2*i]=v&255;p[2*i+1]=v>>8;}
}
static int sink(void *,const unsigned char *p,std::size_t n){accepted.insert(accepted.end(),p,p+n);return mode==3?-EIO:0;}
static int stop(void *){return cancelled?-ECANCELED:0;}
static void state(void *,unsigned event){if(event==1)ready=true;else if(event==2){assert(ready);speech=true;}else assert(event==3);}
extern "C" int a733_pcm_capture_stream(unsigned,unsigned,a733_pcm_capture_sink,void *,uint32_t *,unsigned *){return -ENOTSUP;}
extern "C" int a733_pcm_monitor(unsigned gain,a733_pcm_capture_sink consume,void *cookie,uint64_t *frames,unsigned *ended){
  assert(gain==5);*frames=*ended=0;unsigned char p[960];wire.clear();
  // 12 seconds synthetic idle (not a fixed 8s window), then 1s speech,
  // a 500ms intra-sentence pause, more speech and trailing silence.
  for(unsigned i=0;i<2300;i++) {
    int amplitude=i<1200?0:i<1300?2000:i<1350?0:i<1450?2000:0;
    if(mode==1 && i>=1200)amplitude=2000; // overlong utterance
    if(mode==11)amplitude=i<100?2000:0; // next round speaks immediately after HW warmup
    if(mode==2 && i==100)cancelled=1;
    if(mode==4 && i==1300)return -EOVERFLOW;
    block(p,amplitude,200);wire.insert(wire.end(),p,p+960);
    int r=consume(cookie,p,sizeof(p));*frames+=480;
    if(r<0)return r;
    if(r==1){*ended=1;return 0;}
    usleep(20);
  }
  return -ETIMEDOUT;
}
int main(){
  auto g=std::make_unique<DialogueGate>();unsigned char pcm[960];
  std::vector<unsigned char> emitted;
  auto collect=[](void *p,const unsigned char *s,std::size_t n)->int{
    auto *v=static_cast<std::vector<unsigned char> *>(p);v->insert(v->end(),s,s+n);return 0;};
  block(pcm,0,10000); // DC is NOT acoustic energy.
  for(unsigned i=0;i<12000;i++)assert(!g->push(pcm,960,collect,&emitted));
  assert(g->ready&&!g->speech&&emitted.empty());
  block(pcm,2000);for(unsigned i=0;i<10;i++)assert(!g->push(pcm,960,collect,&emitted));
  block(pcm,0);for(unsigned i=0;i<100;i++)assert(!g->push(pcm,960,collect,&emitted));
  assert(!g->speech&&emitted.empty()); // reject 100ms impulse
  block(pcm,2000);for(unsigned i=0;i<16;i++)assert(!g->push(pcm,960,collect,&emitted));
  assert(g->speech&&emitted.size()==40*960);
  block(pcm,0);for(unsigned i=0;i<69;i++)assert(!g->push(pcm,960,collect,&emitted));
  assert(g->push(pcm,960,collect,&emitted)==1 && g->ended && !g->limited);
  assert(g->push(pcm,960,collect,&emitted)==-EINVAL);
  // A warm next round reuses only the noise estimate; early short speech is
  // retained rather than discarded into a fresh 400ms calibration window.
  g=std::make_unique<DialogueGate>();g->noise=0;g->ready=true;emitted.clear();
  block(pcm,2000);for(unsigned i=0;i<16;i++)assert(!g->push(pcm,960,collect,&emitted));
  assert(g->speech && emitted.size()==16*960);
  // Quiet/sub-trigger speech must not inflate noise until normal speech can
  // never cross the trigger. Raw DC remains excluded from gate energy.
  g=std::make_unique<DialogueGate>();emitted.clear();
  block(pcm,100,200);for(unsigned i=0;i<40;i++)assert(!g->push(pcm,960,collect,&emitted));
  block(pcm,300,200);for(unsigned i=0;i<200;i++)assert(!g->push(pcm,960,collect,&emitted));
  assert(!g->speech);
  block(pcm,600,200);for(unsigned i=0;i<16;i++)assert(!g->push(pcm,960,collect,&emitted));
  assert(g->speech && g->last_peak==800);
  // Reproduce the board's181 RMS floor /251..464 RMS quiet fluctuations.
  // Old2x-noise continuation resets quiet on450 and always hits8s discard.
  g=std::make_unique<DialogueGate>();emitted.clear();
  block(pcm,180);for(unsigned i=0;i<40;i++)assert(!g->push(pcm,960,collect,&emitted));
  block(pcm,1000);for(unsigned i=0;i<60;i++)assert(!g->push(pcm,960,collect,&emitted));
  for(unsigned i=0;i<70;i++) {
    block(pcm,i%4?300:450);
    int rc=g->push(pcm,960,collect,&emitted);assert(rc==(i==69?1:0));
  }
  assert(g->ended&&!g->limited&&g->quiet==70);
  puts("PASS v221fix1: sub-trigger room-noise fluctuations end after700ms, not8s discard");
  // Above-trigger isolated spikes reproduce what v223 did not cover. They
  // must never reset quiet. Pending high blocks are not counted as silence.
  for(unsigned burst=1;burst<8;burst++) {
    g=std::make_unique<DialogueGate>();emitted.clear();
    block(pcm,150);for(unsigned i=0;i<40;i++)assert(!g->push(pcm,960,collect,&emitted));
    block(pcm,1000);for(unsigned i=0;i<60;i++)assert(!g->push(pcm,960,collect,&emitted));
    for(unsigned i=0;!g->ended&&i<700;i++) {
      block(pcm,i%(burst+4)<burst?600:250);
      assert(g->push(pcm,960,collect,&emitted)>=0);
    }
    assert(g->ended&&!g->limited&&g->quiet==70&&g->rejected_bursts>0);
  }
  // A resumed 80ms syllable after500ms pause DOES reset silence. Neither
  // the 500ms gap nor any of its following 80ms speech is cut/discarded.
  g=std::make_unique<DialogueGate>();emitted.clear();g->ready=true;g->noise=0;
  block(pcm,1000);for(unsigned i=0;i<50;i++)assert(!g->push(pcm,960,collect,&emitted));
  block(pcm,150);for(unsigned i=0;i<50;i++)assert(!g->push(pcm,960,collect,&emitted));
  assert(g->quiet==50);
  block(pcm,1000);for(unsigned i=0;i<8;i++)assert(!g->push(pcm,960,collect,&emitted));
  assert(g->quiet==0&&g->endpoint_resets==1);
  block(pcm,150);for(unsigned i=0;i<70;i++)assert(g->push(pcm,960,collect,&emitted)==(i==69?1:0));
  assert(g->ended&&!g->limited);
  puts("PASS v224: 10..70ms above-trigger spikes do not reset quiet;80ms resumed speech does; PCM remains complete");
  LiveCaptureMetrics m{};
  assert(!audio_capture_dialogue(5,400,sink,nullptr,stop,nullptr,state,nullptr,&m));
  assert(ready&&speech&&m.ended&&!m.limited&&m.monitor_frames>12*48000);
  // Includes the full 400ms prefix at trigger and does not end at 500ms gap.
  assert(m.samples48==(40+84+50+100+70)*480 && m.samples16==m.samples48/3);
  assert(accepted.size()==m.samples16*2);
  std::vector<unsigned char> oracle(accepted.size());std::size_t used=0;
  assert(!audio_decimate(wire.data()+(m.monitor_frames-m.samples48)*2,m.samples48*2,
    oracle.data(),oracle.size(),&used) && used==accepted.size() && oracle==accepted);
  uint64_t noise=0;mode=11;accepted.clear();ready=speech=false;
  assert(!audio_capture_dialogue(5,400,sink,nullptr,stop,nullptr,state,nullptr,&m,&noise));
  assert(m.samples48==170*480 && m.monitor_frames==m.samples48 && ready&&speech&&noise==0);
  for(mode=1;mode<=4;mode++) {
    cancelled=0;accepted.clear();ready=speech=false;
    int r=audio_capture_dialogue(5,400,sink,nullptr,stop,nullptr,state,nullptr,&m);
    if(mode==1)assert(!r&&m.limited&&m.samples48==384000);
    if(mode==2)assert(r==-ECANCELED&&!m.consumer_retained);
    if(mode==3)assert(r==-EIO&&!m.consumer_retained);
    if(mode==4)assert(r==-EOVERFLOW&&!m.consumer_retained);
  }
  mode=0;cancelled=0;join_fail=true;__lsan_disable();
  int r=audio_capture_dialogue(5,400,sink,nullptr,stop,nullptr,state,nullptr,&m);__lsan_enable();
  assert(r==-EOWNERDEAD&&m.consumer_retained);assert(!__real_pthread_join(kept,nullptr));
  puts("PASS v214 production gate/capture: unbounded idle, DC rejection, impulse rejection, opening pre-roll, short speech, 500ms pause, 700ms endpoint, 8s discard flag, cancel/overflow/sink/retained owner; HOST ONLY");
}
