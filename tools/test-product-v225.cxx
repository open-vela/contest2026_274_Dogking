/* SPDX-License-Identifier: Apache-2.0 */
#include "product_core.hxx"
#include "mcu_protocol.hxx"
#include <cassert>
#include <cstdio>
using namespace aipet;
struct Routes:ProductPorts {
  int seen=0;std::string reply;
  int vision(std::string &s)override{seen=1;s="检测到人。";return 0;}
  bool motor(const char *s,std::string &out,int &rc)override {
    McuCommand c;int n=mcu_intent(s,c);if(!n)return false;
    seen=2;rc=n<0?-22:0;out=n<0?"动作不明确。":"已接收。";return true;
  }
  bool setting(const char *s,std::string &out,int &rc)override {
    SettingIntent i;if(!product_setting_intent(s,i))return false;seen=3;out="已设置。";rc=0;return true;
  }
  int chat(ProductRoute,const char *,std::string &s,bool &)override{seen=4;s="聊天。";return 0;}
  int speak(ProductRoute,const Reply &s)override{reply=s.text;return 0;}
};
int main(){
  unsigned cases=0;
  for(auto s:{"识别前面物体","识别前面的物体","请帮我识别一下前面的物体吧","vela，看看这是什么？","桌宠vela帮我识别图片","打开摄像头","识图","用摄像头识别周围环境","前面是什么东西？","前面有个什么物体","前面有什么物体"}) {
    Routes p;assert(product_turn(ProductRoute::local,s,p)==0&&p.seen==1);++cases;
  }
  for(auto s:{"不要识别前面物体","我刚才说了识别图片","如何识别图片","识别物体然后前进","你为什么没有识别物体"})assert(!product_vision_intent(s));
  for(auto s:{"前进十五部","前进十五步","你好，前进十五步","帮我向前走十五步吧","千金十五步"}) {
    McuCommand c;assert(mcu_intent(s,c)==1&&c.op==1&&c.payload==std::vector<uint8_t>({0,2,44,1}));++cases;
  }
  for(unsigned n:{1,3,20,102,512}) {
    McuCommand c;assert(mcu_intent("前进"+std::to_string(n)+"步",c)==1);
    assert((unsigned(c.payload[2])|(unsigned(c.payload[3])<<8))==n*20);++cases;
  }
  for(auto s:{"不要前进三步","前进十五步然后左转","前进0步","前进513步","前进1025步","前进65535步","为什么前进三步","前进一百二步"}){McuCommand c;assert(mcu_intent(s,c)<0);}
  McuCommand c;assert(mcu_move(0,2,10240,c)&&!mcu_move(0,2,10241,c)&&!mcu_move(0,2,65535,c));
  for(auto s:{"音量调到五十","请帮我音量调到百分之五十吧","设置音量50%","音量五十"}) {
    SettingIntent i;assert(product_setting_intent(s,i)&&i.kind==SettingKind::volume&&i.value==50);
    Routes p;assert(!product_turn(ProductRoute::cloud,s,p)&&p.seen==3);++cases;
  }
  for(auto s:{"大声一点","静音","取消静音","当前音量","麦克风增益五"}) {SettingIntent i;assert(product_setting_intent(s,i));++cases;}
  for(auto s:{"不要调大音量","音量101","音量-1","麦克风增益零","麦克风增益十七","我觉得音量太小怎么办","音量五十然后前进"}){SettingIntent i;assert(!product_setting_intent(s,i));}
  Routes p;assert(!product_turn(ProductRoute::local,"我今天有点难过",p)&&p.seen==4);
  std::printf("PASS inherited v225 routes with v227 units: %u positive intent cases plus negatives; x20, 512-step/10240-pulse bound, unchanged 300Hz. HOST, not hardware.\n",cases);
}
