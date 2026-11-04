/* SPDX-License-Identifier: Apache-2.0 */
#include "product_core.hxx"
#include "product_labels.hxx"
#include "mcu_protocol.hxx"
#include <cassert>
#include <cerrno>
#include <cstdio>
using namespace aipet;
struct Mock final:ProductPorts {
  unsigned cameras=0,moves=0,chats=0,speeches=0;int camera_error=0,chat_error=0,speech_error=0;
  bool stream=false;int cancellation=0;ProductRoute spoken=ProductRoute::cloud;std::string speech;
  int cancelled()override{return cancellation;}
  int vision(std::string &out)override{++cameras;out="模型检测到狗。";return camera_error;}
  bool motor(const char *text,std::string &out,int &rc)override {
    McuCommand cmd;int match=mcu_intent(text,cmd);if(!match)return false;
    if(match<0){out="指令不明确，未执行。";rc=-EINVAL;return true;}
    ++moves;out="下位机已接受指令，但不代表动作已完成。";return true;
  }
  int chat(ProductRoute,const char *,std::string &out,bool &done)override{++chats;out="陪着你。";done=stream;return chat_error;}
  int speak(ProductRoute route,const Reply &r)override{++speeches;spoken=route;speech=r.text;return speech_error;}
};
int main() {
  for(int cause:{0,-ECANCELED,-ETIMEDOUT})assert(product_capture_outcome(-EOWNERDEAD,cause)==-EOWNERDEAD);
  assert(product_capture_outcome(-ENODATA,-ECANCELED)==-ECANCELED);
  assert(product_capture_outcome(-EPROTO,0)==-EPROTO);
  Mock cancelled;cancelled.cancellation=-ECANCELED;
  assert(product_turn(ProductRoute::local,"前进",cancelled)==-ECANCELED&&!cancelled.moves&&!cancelled.speeches);
  for(int network=0;network<2;network++)for(int cloud=0;cloud<2;cloud++)for(int local=0;local<2;local++) {
    ProductReadiness ready{bool(network),bool(cloud),bool(local)};
    auto a=product_choose(ProductMode::automatic,ready);assert(a.route==((network&&cloud)?ProductRoute::cloud:ProductRoute::local));
    assert((a.error==0)==((network&&cloud)||local));
    assert(product_choose(ProductMode::local,ready).route==ProductRoute::local);
    auto force=product_choose(ProductMode::cloud,ready);assert(force.route==ProductRoute::cloud);assert((force.error==0)==(network&&cloud));
  }
  for(auto s:{"识别物体","请识别物体。","看看这是什么？","你看到了什么"})assert(product_vision_intent(s));
  for(auto s:{"不要识别物体","如何识别物体","识别物体然后前进","我说识别物体"})assert(!product_vision_intent(s));
  Rules rules;rules.direct={{{"几点","时间","日期","星期几","几号"},{"现在{time}"}}};std::string direct;
  for(auto s:{"现在几点了","请问现在几点了？","今天星期几","今天几号？"})assert(product_direct_rule(rules,s,direct)&&direct=="现在{time}");
  for(auto s:{"时间过得好快，我很难过","不要告诉我时间","我觉得时间不够用"})assert(!product_direct_rule(rules,s,direct));
  for(auto route:{ProductRoute::cloud,ProductRoute::local}) {
    Mock p;assert(product_turn(route,"识别物体",p)==0&&p.cameras==1&&p.speeches==1&&p.chats==0&&p.spoken==route);
    p.camera_error=-ETIMEDOUT;assert(product_turn(route,"识别物体",p)==0&&p.cameras==2&&p.speech.find("没有拿到")!=std::string::npos);
    unsigned spoken=p.speeches;
    p.camera_error=-EOWNERDEAD;assert(product_turn(route,"识别物体",p)==-EOWNERDEAD&&p.speeches==spoken);
    p.camera_error=-ECANCELED;assert(product_turn(route,"识别物体",p)==-ECANCELED&&p.speeches==spoken);
    p.camera_error=0;
    assert(product_turn(route,"前进一百二十三步",p)==0&&p.moves==1&&p.cameras==4);
    assert(product_turn(route,"不要前进",p)==0&&p.moves==1&&p.speech.find("未执行")!=std::string::npos);
    p.stream=true;unsigned before=p.speeches;assert(product_turn(route,"你好",p)==0&&p.speeches==before); // No duplicate TTS
    p.stream=false;p.chat_error=-ETIMEDOUT;assert(product_turn(route,"你好",p)==-ETIMEDOUT&&p.chats==2&&p.speeches==before);
    p.speech_error=-EIO;assert(product_turn(route,"前进",p)==-EIO&&p.moves==2); // Never retry a physical command.
  }
  McuCommand c;
  for(auto s:{"前进十步","前进一百零二步","前进五百一十二步","后退两步","右转二十一步"})assert(mcu_intent(s,c)==1);
  for(auto s:{"前进一百二步","前进一千步然后右转","前进五百一十三步","前进一千零二十四步","前进一千零二十五步","前进零步","前进一百零零二步"})assert(mcu_intent(s,c)==-1);
  a733_live_result r{};r.sequence=1;r.kind=A733_LIVE_RESULT;r.count=1;r.input_crc=123;
  auto &b=r.boxes[0];b.magic=A733_VISION_MAGIC;b.version=A733_VISION_VERSION;b.type=A733_VISION_DETECTION;
  b.sequence=1;b.value=123;b.count=1;b.index=0;b.class_id=16;b.score_milli=910;b.x1=10;b.y1=10;b.x2=100;b.y2=100;
  std::string out;assert(product_describe(r,out)==0&&out.find("狗")!=std::string::npos);
  b.sequence=2;assert(product_describe(r,out)!=0);b.sequence=1;b.value=0;assert(product_describe(r,out)!=0);
  b.value=123;b.class_id=80;assert(product_describe(r,out)!=0);b.class_id=16;b.score_milli=499;
  assert(product_describe(r,out)==0&&out.find("不代表")!=std::string::npos);
  r.count=0;assert(product_describe(r,out)==0);r.kind=A733_LIVE_PREVIEW;assert(product_describe(r,out)!=0);
  for(unsigned i=0;i<80;i++)assert(product_coco_name(i));
  assert(!product_coco_name(80));
  std::puts("PASS product v1: frozen cloud/local policy, exact vision/motion intents, Chinese bounded steps, no duplicate speech/action replay, fresh same-frame COCO labels, truthful failures. HOST MOCKS, NOT hardware.");
}
