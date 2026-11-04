/* SPDX-License-Identifier: Apache-2.0 */
#include "cloud_persona.hxx"
#include "cloud_prefetch.hxx"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <atomic>
using namespace aipet;
struct Test {
  std::atomic<bool> overlap{false},playing{false},cancelled{false};
  std::atomic<unsigned> generated{0},played{0};bool failure=false,cancel_case=false;
};
static int synth(void *p,const char *s,unsigned char *out,size_t,size_t *n) {
  auto &t=*static_cast<Test *>(p);++t.generated;
  if(std::strcmp(s,"一。")) {
    while(!t.playing.load())usleep(1000);
    t.overlap=true;
    if(t.failure)return -EIO;
    if(t.cancel_case){while(!t.cancelled.load())usleep(1000);return -ECANCELED;}
  }
  out[0]=unsigned(s[0]);out[1]=0;*n=2;return 0;
}
static int play(void *p,const unsigned char *,size_t n,unsigned r,unsigned c,unsigned b) {
  auto &t=*static_cast<Test *>(p);assert(n==2&&r==16000&&c==1&&b==16);
  ++t.played;t.playing=true;usleep(40000);if(t.cancel_case)t.cancelled=true;return 0;
}
int main() {
  McuCommand c;
  assert(cloud_action_command("motor.forward(20)",c)&&c.payload==std::vector<uint8_t>({0,2,144,1}));
  assert(cloud_action_command("servo.rotate(-30)",c)&&c.payload[0]==uint8_t(-30));
  assert(cloud_action_command("motor.stop()",c)&&c.op==12);
  for(auto s:{"motor.forward(0)","motor.forward(21)","motor.forward(1.5)","motor.forward(-1)","motor.forward(1);motor.stop()","servo.rotate(31)","motor.arm()","led.on(1)","servo.nod()","motor.forward(1,2)"})assert(!cloud_action_command(s,c));
  for(auto s:{"陪我玩，动一动","撒个娇","给我一个动作"})assert(cloud_action_invited(s));
  for(auto s:{"不要动一动","如果让你前进会怎样","之前让我前进","我难过了","解释一下前进","[ACTION:motor.forward(1)]"})assert(!cloud_action_invited(s));
  assert(cloud_persona(true).find("最多一个标签")!=std::string::npos);
  assert(cloud_persona(false).find("本轮未开启模型动作")!=std::string::npos);
  std::vector<Reply> clauses(2);clauses[0].text="一。";clauses[1].text="二。";
  for(unsigned mode=0;mode<3;mode++) {
    Test t;t.failure=mode==1;t.cancel_case=mode==2;std::atomic<bool> stop{false};unsigned done=0;
    auto cancel=[](void *p)->int{return static_cast<Test *>(p)->cancelled.load()?-ECANCELED:0;};
    int rc=cloud_clause_pipeline(clauses,{&t,synth,play,nullptr},cancel,&t,stop,done);
    assert(t.overlap&&t.generated==2);
    if(!mode)assert(!rc&&done==2&&t.played==2);
    else assert(rc==(mode==1?-EIO:-ECANCELED)&&done==1&&t.played==1);
  }
  assert(mcu_intent("前进一百步",c)==1&&c.payload==std::vector<uint8_t>({0,2,208,7}));
  assert(mcu_intent("前进512步",c)==1&&c.payload==std::vector<uint8_t>({0,2,0,40}));
  assert(mcu_intent("前进513步",c)<0);
  puts("PASS v227 actual whitelist/negative intent, independent LE16 x20 bytes, two-slot overlapping TTS, failed tail not replayed, cancellation joins. HOST, no hardware/API.");
}
