/* SPDX-License-Identifier: Apache-2.0 */
#include "mcu_protocol.hxx"
#include <cassert>
#include <cstring>
#include <cstdio>
extern "C" {
#include "link_proto.h"
}
int main()
{
  using namespace aipet;
  assert(mcu_crc(reinterpret_cast<const uint8_t *>("123456789"),9)==0x29b1);
  uint32_t random=7;
  auto rng=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return random;};
  for(unsigned n=0;n<=200;n++)for(unsigned kind=1;kind<=3;kind++) {
    McuFrame f;f.type=kind;f.seq=uint16_t(rng());f.op=uint8_t(rng());f.payload.resize(n);
    for(auto &v:f.payload)v=uint8_t(rng());
    std::vector<uint8_t> raw;assert(mcu_encode(f,raw));uint8_t reference[208];
    int size=Link_BuildFrame(reference,sizeof(reference),f.type,f.seq,f.op,f.payload.data(),uint8_t(n));
    assert(size==int(raw.size())&&!std::memcmp(raw.data(),reference,size));
    McuParser parser;McuFrame got;
    for(size_t i=0;i<raw.size();i++)assert(parser.feed(raw[i],100+i,got)==(i+1==raw.size()));
    assert(got.type==f.type&&got.seq==f.seq&&got.op==f.op&&got.payload==f.payload);
    raw.back()^=1;for(auto b:raw)assert(!parser.feed(b,400,got));
    raw.back()^=1;for(size_t i=0;i<raw.size();i++)assert(parser.feed(raw[i],600+i,got)==(i+1==raw.size()));
  }
  McuParser parser;McuFrame f;McuCommand c;
  parser.feed(0xa5,1,f);parser.feed(1,2,f);parser.feed(0xa5,200,f);
  std::vector<uint8_t> frame;assert(mcu_encode({2,0x86,42,{}},frame));
  for(size_t i=1;i<frame.size();i++)assert(parser.feed(frame[i],201+i,f)==(i+1==frame.size()));
  for(unsigned i=0;i<100000;i++)parser.feed(uint8_t(rng()),1000+i,f);
  assert(!mcu_move(4,1,256,c)&&!mcu_move(0,4,1,c)&&!mcu_move(0,1,0,c)&&!mcu_move(0,1,65535,c));
  assert(!mcu_servo(-46,500,c)&&!mcu_servo(0,99,c)&&!mcu_servo(0,2001,c));
  for(auto s:{"前进","前进256步","请前进！","后退","左转","右转","停止","向左看","向右看","回中"})assert(mcu_intent(s,c)==1);
  for(auto s:{"不要前进","前进然后左转","前进0步","前进1025步","前进65535步","前进1米","为什么前进","前进-2步"})assert(mcu_intent(s,c)==-1);
  assert(mcu_intent("你好",c)==0&&mcu_intent("讲个故事",c)==0);
  puts("PASS: 603 frames byte-exact against pinned STM32 C encoder, CRC/fuzz/timeouts/intent limits");
}
