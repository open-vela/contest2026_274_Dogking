/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <utility>
namespace aipet {
constexpr unsigned mcu_step_scale=20; // v227: double v225 voice travel; UI/voice/tool share units.
constexpr unsigned mcu_step_max=512;  // preserve the qualified 10240-pulse single-packet bound.
constexpr unsigned mcu_default_pulses=512;
/* v225: deterministic command grammar shared by voice and typed requests.
 * Never fuzzy substring-execute a quoted, negated or compound instruction. */
inline std::string command_text(std::string s) {
  for(auto p:{" ","\t","\r","\n","，",",","。","！","!","？","?"}) {
    std::size_t at;while((at=s.find(p))!=std::string::npos)s.erase(at,std::char_traits<char>::length(p));
  }
  for(char &c:s)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
  for(unsigned round=0;round<8;round++) {
    bool changed=false;
    for(auto p:{"桌宠vela","你好vela","vela","小派","维拉","薇拉","桌宠","我想请你","我想让你","麻烦你","麻烦","请帮我","帮我","帮忙","请问","请你","请","给我","能不能"}) {
      auto n=std::char_traits<char>::length(p);if(s.compare(0,n,p)==0){s.erase(0,n);changed=true;break;}
    }
    if(!changed)break;
  }
  for(unsigned round=0;round<3;round++) {
    bool changed=false;
    for(auto p:{"一下吧","好不好","好吗","一下","吧","呀"}) {
      auto n=std::char_traits<char>::length(p);if(s.size()>n&&s.compare(s.size()-n,n,p)==0){s.resize(s.size()-n);changed=true;break;}
    }
    if(!changed)break;
  }
  return s;
}
inline bool command_uint(const std::string &s,unsigned maximum,unsigned &out) {
  out=0;if(s.empty()||s.size()>36)return false;
  if(s[0]>='0'&&s[0]<='9') {
    for(char c:s){if(c<'0'||c>'9'||out>maximum/10||(out==maximum/10&&unsigned(c-'0')>maximum%10))return false;out=out*10+unsigned(c-'0');}
    return true;
  }
  if(s=="零"){out=0;return true;}
  unsigned total=0,digit=0,last=10000;bool zero=false,unit_seen=false;
  for(std::size_t pos=0;pos<s.size();) {
    int value=-1;std::size_t width=0;
    const char *digits[]={"零","一","二","三","四","五","六","七","八","九","两"};
    for(unsigned i=0;i<11;i++){auto n=std::char_traits<char>::length(digits[i]);if(s.compare(pos,n,digits[i])==0){value=i==10?2:i;width=n;break;}}
    if(value>=0) {
      if(!value){if(!unit_seen||digit||zero)return false;zero=true;}
      else {if(digit)return false;digit=unsigned(value);}
      pos+=width;continue;
    }
    unsigned unit=0;
    for(auto p:{std::pair<const char *,unsigned>{"十",10},{"百",100},{"千",1000}}) {
      auto n=std::char_traits<char>::length(p.first);if(s.compare(pos,n,p.first)==0){unit=p.second;width=n;break;}
    }
    if(!unit||unit>=last)return false;
    if(!digit){if(unit==10&&!pos)digit=1;else return false;}
    total+=digit*unit;if(total>maximum)return false;
    digit=0;last=unit;unit_seen=true;zero=false;pos+=width;
  }
  if((zero&&!digit)||(digit&&unit_seen&&last>10&&!zero))return false;
  out=total+digit;return out<=maximum;
}
struct McuFrame {
  uint8_t type=0, op=0;
  uint16_t seq=0;
  std::vector<uint8_t> payload;
};
uint16_t mcu_crc(const uint8_t *,size_t);
bool mcu_encode(const McuFrame &,std::vector<uint8_t> &);
class McuParser {
public:
  bool feed(uint8_t,uint64_t,McuFrame &);
  unsigned bad=0;
private:
  std::array<uint8_t,208> raw_{};
  size_t used_=0;
  uint64_t last_=0;
};
struct McuCommand {
  uint8_t op=6;
  std::vector<uint8_t> payload;
};
bool mcu_move(unsigned dir,unsigned speed,unsigned steps,McuCommand &);
bool mcu_servo(int degrees,unsigned ms,McuCommand &);
/* 0 ordinary conversation; 1 exact bounded control; -1 ambiguous/negated control. */
int mcu_intent(const std::string &,McuCommand &);
bool mcu_same(const McuCommand &,const McuCommand &);
}
