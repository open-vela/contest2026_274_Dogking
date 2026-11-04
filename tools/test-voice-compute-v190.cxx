/* SPDX-License-Identifier: Apache-2.0 */
/* HOST fault injection only; never changes real CPU affinity. */
#include <sched.h>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdint>
static cpu_set_t mask;
static int get_error=0,set_error=0,sets=0;
static unsigned actual=0,waits=0;
static uint64_t ticks=0;
static bool stuck=false;
static int aipet_compute_test_identity(uint64_t *midr,uint64_t *mpidr) {
  *mpidr=uint64_t(actual)<<8;*midr=uint64_t(actual<6?0xd05:0xd0b)<<4;return 0;
}
static uint64_t aipet_compute_test_time(){return ticks;}
static void aipet_compute_test_wait() {
  ++waits;ticks+=1000;if(!stuck)for(unsigned i=0;i<8;i++)if(CPU_ISSET(i,&mask)){actual=i;break;}
}
static int fake_get(pid_t,std::size_t,cpu_set_t *out) {
  if(get_error){errno=get_error;return -1;}*out=mask;return 0;
}
static int fake_set(pid_t,std::size_t,const cpu_set_t *value) {
  ++sets;if(set_error){errno=set_error;return -1;}mask=*value;return 0;
}
#define AIPET_TEST_COMPUTE 1
#define sched_getaffinity fake_get
#define sched_setaffinity fake_set
#include "voice_compute.hxx"
#undef sched_getaffinity
#undef sched_setaffinity
int main() {
  CPU_ZERO(&mask);CPU_SET(0,&mask);CPU_SET(2,&mask);cpu_set_t original=mask;
  {aipet::VoiceCompute s;assert(!s.begin());assert(CPU_ISSET(7,&mask)&&CPU_COUNT(&mask)==1 && actual==7 && waits>=2);
   assert(!s.finish(0));assert(CPU_EQUAL(&mask,&original));}
  {aipet::VoiceCompute s;assert(!s.begin());assert(s.finish(-EIO)==-EIO);assert(CPU_EQUAL(&mask,&original));}
  {aipet::VoiceCompute s;assert(!s.begin());}assert(CPU_EQUAL(&mask,&original));
  {aipet::VoiceCompute s(6);assert(!s.begin() && actual==6);assert(!s.finish(0) && CPU_EQUAL(&mask,&original));}
  {aipet::VoiceCompute s(1,0xd05);assert(!s.begin() && actual==1);assert(!s.finish(0) && CPU_EQUAL(&mask,&original));}
  stuck=true;actual=0;unsigned before_waits=waits;
  {aipet::VoiceCompute s;assert(s.begin()==-ETIMEDOUT);}
  assert(CPU_EQUAL(&mask,&original) && waits-before_waits<=251);stuck=false;
  int before=sets;get_error=EIO;{aipet::VoiceCompute s;assert(s.begin()==-EIO);}assert(sets==before);get_error=0;
  set_error=EPERM;{aipet::VoiceCompute s;assert(s.begin()==-EPERM);}set_error=0;
  {aipet::VoiceCompute s;assert(!s.begin());set_error=EIO;assert(s.finish(0)==-EIO);set_error=0;}
  puts("PASS: scoped compute mask selection/restoration and get/set/restore failures (host mocks, no real affinity changes)");
}
