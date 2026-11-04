/* SPDX-License-Identifier: Apache-2.0 */
/* Independent A7Z codec. Wire contract: STM32C8T6Mini-Play efdb4379,
 * Protocol/link_proto.c + User/app_config.h + Service/link_service.c.
 * No downstream source is copied into the production build. */
#include "mcu_protocol.hxx"
#include <algorithm>
#include <cstring>
namespace aipet {
uint16_t mcu_crc(const uint8_t *p,size_t n)
{
  uint16_t c=0xffff;
  for(size_t i=0;i<n;i++) {
    c^=uint16_t(p[i])<<8;
    for(unsigned j=0;j<8;j++) c=uint16_t((c<<1)^((c&0x8000)?0x1021:0));
  }
  return c;
}
bool mcu_encode(const McuFrame &f,std::vector<uint8_t> &out)
{
  out.clear();
  if(f.type<1 || f.type>3 || f.payload.size()>200) return false;
  out={0xa5,f.type,uint8_t(f.seq),uint8_t(f.seq>>8),f.op,uint8_t(f.payload.size())};
  out.insert(out.end(),f.payload.begin(),f.payload.end());
  uint16_t c=mcu_crc(out.data(),out.size());
  out.push_back(uint8_t(c));out.push_back(uint8_t(c>>8));return true;
}
bool McuParser::feed(uint8_t v,uint64_t now,McuFrame &out)
{
  if(used_ && now-last_>100) {used_=0;bad++;}
  last_=now;
  if(!used_ && v!=0xa5)return false;
  raw_[used_++]=v;
  if((used_==2 && (v<1||v>3)) || (used_==6 && v>200)) {used_=0;bad++;return false;}
  if(used_<8 || used_<size_t(raw_[5])+8)return false;
  size_t n=used_;used_=0;
  if(mcu_crc(raw_.data(),n-2)!=(unsigned(raw_[n-2])|(unsigned(raw_[n-1])<<8))) {
    bad++;return false;
  }
  McuFrame f;f.type=raw_[1];f.seq=uint16_t(raw_[2])|(uint16_t(raw_[3])<<8);
  f.op=raw_[4];f.payload.assign(raw_.begin()+6,raw_.begin()+n-2);
  out=std::move(f);return true;
}
bool mcu_move(unsigned dir,unsigned speed,unsigned steps,McuCommand &out)
{
  if(dir>3||speed>3||steps<1||steps>10240)return false; // v225: 1024 spoken steps x10, never continuous.
  out={1,{uint8_t(dir),uint8_t(speed),uint8_t(steps),uint8_t(steps>>8)}};return true;
}
bool mcu_servo(int degrees,unsigned ms,McuCommand &out)
{
  if(degrees< -45||degrees>45||ms<100||ms>2000)return false;
  out={2,{uint8_t(degrees),uint8_t(ms),uint8_t(ms>>8)}};return true;
}
bool mcu_same(const McuCommand &a,const McuCommand &b)
{return a.op==b.op&&a.payload==b.payload;}
static bool voice_steps(const std::string &s,unsigned &out)
{
  out=0;if(s.empty()||s.size()>36)return false;
  if(s[0]>='0'&&s[0]<='9') {
    if(s.size()>4)return false;
    for(char c:s){if(c<'0'||c>'9')return false;out=out*10+unsigned(c-'0');}
    return out>=1&&out<=mcu_step_max;
  }
  unsigned total=0,digit=0,last_unit=10000;bool zero=false,have_unit=false;
  for(size_t pos=0;pos<s.size();) {
    int value=-1;size_t width=0;
    const char *digits[]={"零","一","二","三","四","五","六","七","八","九","两"};
    for(unsigned i=0;i<11;i++)if(s.compare(pos,std::strlen(digits[i]),digits[i])==0){value=i==10?2:i;width=std::strlen(digits[i]);break;}
    if(value>=0) {
      if(value==0){if(!have_unit||digit||zero)return false;zero=true;}
      else {if(digit)return false;digit=value;}
      pos+=width;continue;
    }
    unsigned unit=0;
    for(auto pair:{std::pair<const char *,unsigned>{"十",10},{"百",100},{"千",1000}})
      if(s.compare(pos,std::strlen(pair.first),pair.first)==0){unit=pair.second;width=std::strlen(pair.first);break;}
    if(!unit||unit>=last_unit)return false;
    if(!digit){if(unit==10&&!pos)digit=1;else return false;}
    total+=digit*unit;if(total>1024)return false;
    digit=0;last_unit=unit;have_unit=true;zero=false;pos+=width;
  }
  if(zero&&!digit)return false;
  // Reject ambiguous colloquial "一百二" / "一千二" instead of moving an
  // unintended distance. ASR must provide 一百零二 / 一百二十 explicitly.
  if(digit&&have_unit&&last_unit>10&&!zero)return false;
  out=total+digit;return out>=1&&out<=mcu_step_max;
}
int mcu_intent(const std::string &text,McuCommand &out)
{
  if(text.size()>4096 || text.find('\0')!=std::string::npos)return -1;
  std::string s=command_text(text);
  // ASR-observed homophones are accepted ONLY as a complete bounded MOVE.
  if(s.compare(0,6,"你好")==0&&s.size()>6)s.erase(0,6);
  for(const char *p:{"请你","请","小派，","小派","让桌宠","桌宠"})
    if(s.compare(0,std::strlen(p),p)==0)s.erase(0,std::strlen(p));
  while(!s.empty() && s.front()==' ')s.erase(0,1);
  for(bool again=true;again;) {
    again=false;
    for(const char *p:{"。","！","!"," ","，"}) {
      size_t n=std::strlen(p);
      if(s.size()>=n && s.compare(s.size()-n,n,p)==0) {s.resize(s.size()-n);again=true;}
    }
  }
  for(const char *p:{"停止","停下","急停","停止运动","停止电机","stop"})
    if(s==p){out={12,{}};return 1;}
  for(const char *p:{"下位机状态","查询下位机状态","机器人状态"})
    if(s==p){out={6,{}};return 1;}
  if(s=="电机状态"||s=="查询电机状态"){out={9,{}};return 1;}
  if(s=="链路状态"||s=="查询链路状态"){out={10,{}};return 1;}
  if(s=="下位机配置"||s=="查询下位机配置"){out={8,{}};return 1;}
  for(const char *p:{"向左看","往左看","舵机左转"})
    if(s==p){mcu_servo(-30,500,out);return 1;}
  for(const char *p:{"向右看","往右看","舵机右转"})
    if(s==p){mcu_servo(30,500,out);return 1;}
  if(s=="回中"||s=="舵机回中"||s=="看前面") {mcu_servo(0,500,out);return 1;}
  const char *directions[]={"前进","后退","左转","右转","向前走","往前走","向后退","往后退","向左转","往左转","向右转","往右转","千金"};
  const unsigned direction_ids[]={0,1,2,3,0,0,1,1,2,2,3,3,0};
  for(unsigned d=0;d<13;d++) {
    unsigned i=direction_ids[d];std::string prefix=directions[d];
    if(d<12&&(s==prefix||s==prefix+"一点")) {mcu_move(i,2,mcu_default_pulses,out);return 1;}
    if(s.compare(0,prefix.size(),prefix)==0 && s.size()>prefix.size()+3 &&
       (s.compare(s.size()-3,3,"步")==0||s.compare(s.size()-3,3,"部")==0)) {
      std::string n=s.substr(prefix.size(),s.size()-prefix.size()-3);
      unsigned count=0;if(!voice_steps(n,count))return -1;
      return mcu_move(i,2,count*mcu_step_scale,out)?1:-1;
    }
  }
  // No substring execution: questions, negatives, compounds and distances need clarification.
  for(const char *p:{"前进","后退","左转","右转","舵机","向左看","向右看","停下","急停","停止运动"})
    if(s.find(p)!=std::string::npos)return -1;
  return 0;
}
}
