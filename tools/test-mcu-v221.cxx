/* SPDX-License-Identifier: Apache-2.0 */
// Actual production state machine, no physical UART/motor/TOF execution.
#include "../board/a733-cubie-a7z/openvela-overlay/apps/system/aipet/mcu_link.cxx"
#include <cassert>
namespace aipet { int run_test(){
  using namespace aipet;
  link.seq=65534;assert(next_seq()==65535&&next_seq()==1&&next_seq()==2);
  link.ready=true;link.seen=true;link.pong_ms=100;link.impl=0x3f;link.config=0x4e;link.fault=0;
  assert(!allowed({8,{}},100)&&!allowed({9,{}},100)&&!allowed({10,{}},100));
  assert(!allowed({5,{0}},100)&&!allowed({5,{0,0x5a}},100));
  assert(allowed({5,{3}},100)!=0);
  assert(!allowed({7,{0x4f,0,0,0}},100));link.armed=true;
  assert(allowed({7,{0x4f,0,0,0}},100)!=0);
  link.pending=link.sent=true;link.done=false;link.deadline=1000;link.job_seq=42;link.command={9,{}};
  receive({2,9,41,std::vector<uint8_t>(24)},101);assert(!link.done);
  receive({2,9,42,std::vector<uint8_t>(23)},101);assert(!link.done);
  receive({2,9,42,std::vector<uint8_t>(24)},101);assert(link.done&&!link.result);
  link.done=false;link.command={1,{0,1,0,1}};
  receive({3,0x91,1,{0,99,0}},102);assert(link.armed&&!link.done);
  McuParser parser;McuFrame got;std::vector<uint8_t> batch,part;
  for(auto f:{McuFrame{2,0x81,42,{0,255}},McuFrame{3,0x92,0,{9,25,0,255,255,255,255,100,0}},
              McuFrame{3,0x94,0,{0,255,255,255,255}},McuFrame{3,0x94,0,{1,2,0,0,0}}}){
    assert(mcu_encode(f,part));batch.insert(batch.end(),part.begin(),part.end());
  }
  for(auto b:batch)if(parser.feed(b,103,got))receive(got,103);
  assert(link.done && !link.result && link.response.payload[1]==255);
  assert(link.tof_mask==9 && link.distances[0]==25 && link.distances[1]==-1 && link.distances[3]==100);
  assert(link.positions[0]==-1 && link.positions[1]==2 && link.done_events==2);
  link.done=false;receive({3,0x95,0,{50,0}},104);assert(!link.armed&&link.done&&link.result==-ECANCELED);
  link.armed=true;link.done=false;link.command={1,{0,1,1,0}};
  receive({2,0x82,42,{9,1}},105);assert(link.done&&!link.armed&&link.detail==1&&link.status==9);
  link.armed=true;link.done=false;link.command={3,{}};
  receive({2,0x81,42,{2}},106);assert(link.done&&!link.result&&link.armed);
  receive({3,0x96,0,{0x4f,0,0,0}},107);assert(!link.armed&&link.config==0x4f);
  puts("PASS v221: exact query seq/op/length, multi-frame ACK+TOF+two events, invalid distance, signed positions, arbitration/edge/obstacle/config disarm, TTS backpressure; no physical acceptance");
  return 0;
} }
int main(){return aipet::run_test();}
