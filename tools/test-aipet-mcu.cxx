/* SPDX-License-Identifier: Apache-2.0 */
/* Real capture/resample/ASR dispatch/route/UART/TTS code with mocked PCM/cloud,
 * a POSIX PTY MCU and fault injection. This is NOT a physical-board test. */
#define AIPET_MCU_INTEGRATION
#define main unused_legacy_audio_main
#define __wrap_open legacy_audio_open
#include "test-aipet-audio.cxx"
#undef __wrap_open
#undef main
#include "mcu_link.hxx"
#include "tools/tool_registry.h"
#include <atomic>
#include <thread>
#include <pty.h>
#include <chrono>
#include <cJSON.h>
#include <spawn.h>
#ifdef AIPET_MCU_TEST_SPAWN
extern "C" int posix_spawn(pid_t *pid,const char *path,const posix_spawn_file_actions_t *,
                            const posix_spawnattr_t *,char *const args[],char *const [])
{
  assert(!std::strcmp(path,"aipet")&&!std::strcmp(args[1],"--mcu-worker")&&!args[2]);
  *pid=getpid();pthread_attr_t attr;pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);pthread_t t;
  int rc=pthread_create(&t,&attr,[](void *)->void * {assert(aipet::mcu_worker_main()==0);return nullptr;},nullptr);
  pthread_attr_destroy(&attr);return rc;
}
#endif
static int master_fd;
static char slave_name[128];
static std::atomic<bool> finish_peer{false},respond{true},nak{false},wrong_seq{false};
static std::atomic<bool> late_ack{false},fault_before_ack{false};
static std::atomic<unsigned> pings{0},moves{0},servos{0},stops{0},fault{0};
static std::atomic<unsigned> config{1},config_writes{0},tts_frames{0},tof_polls{0};
static std::atomic<unsigned> last_move_pulses{0},last_move_speed{0};
static tool_provider_fn provider;
static tool_executor_fn executor;
static bool use_tool=false,double_tool=false,bad_tool=false;
extern "C" void tool_registry_register_provider(const char *name,tool_provider_fn p,tool_executor_fn e)
{assert(!std::strcmp(name,"stm32-uart4"));provider=p;executor=e;}
extern "C" void tool_registry_invalidate(void) {}
extern "C" int aipet_agent_submit(const char *,uint32_t) {return use_tool?0:-EIO;}
extern "C" int aipet_agent_poll(char *out,size_t cap,uint64_t *)
{
  assert(use_tool&&executor&&cap>=4097);
  char result[512];
  const char *request=bad_tool?"{\"action\":\"forward\",\"steps\":1024}":"{\"action\":\"forward\"}";
  assert(executor("stm32_control",request,result,sizeof(result))==0);
  if(double_tool)assert(executor("stm32_control",request,result,sizeof(result))==0);
  std::strcpy(out,"模型声称已经完成行走");return 0;
}
extern "C" int __real_open(const char *,int,...);
// The legacy open wrapper is compiled under a renamed symbol below by the script;
// this wrapper adds PTY routing while retaining its WAV fixture path adapter.
extern "C" int __wrap_open(const char *path,int flags,...)
{
  int mode=0;if(flags&O_CREAT){va_list a;va_start(a,flags);mode=va_arg(a,int);va_end(a);}
  if(!std::strcmp(path,"/dev/ttyS4"))return __real_open(slave_name,flags,mode);
  return legacy_audio_open(path,flags,mode);
}
static void peer()
{
  aipet::McuParser parser;uint64_t tick=1000;
  while(!finish_peer) {
    uint8_t bytes[256];ssize_t n=read(master_fd,bytes,sizeof(bytes));
    if(n<0){usleep(1000);continue;}
    for(ssize_t i=0;i<n;i++) {
      aipet::McuFrame f;
      if(!parser.feed(bytes[i],tick,f))continue;
      assert(f.type==1);
      aipet::McuFrame reply;reply.type=2;reply.seq=f.seq;
      if(f.op==6) {
        ++pings;reply.op=0x86;
        unsigned uptime=++tick;
        reply.payload={uint8_t(uptime),uint8_t(uptime>>8),0,0,uint8_t(fault.load()),uint8_t(config.load()),0,0,0,2,0x3f};
      } else if(f.op==8) {reply.op=0x87;reply.payload={uint8_t(config.load()),0,0,0};}
      else if(f.op==9) {reply.op=9;reply.payload.resize(24,0);}
      else if(f.op==10) {reply.op=10;reply.payload.resize(32,0);reply.payload[27]=1;}
      else {
        if(f.op==1){assert(f.payload.size()==4);last_move_speed=f.payload[1];last_move_pulses=unsigned(f.payload[2])|(unsigned(f.payload[3])<<8);++moves;}
        else if(f.op==2)++servos;else if(f.op==12)++stops;
        else if(f.op==7){assert(f.payload.size()==4);config=f.payload[0];++config_writes;}
        else if(f.op==4){assert(f.payload==std::vector<uint8_t>{50});++tof_polls;}
        else if(f.op==3){assert(f.payload.size()>=5&&f.payload[0]==0xfd&&f.payload[4]==4);++tts_frames;}
        else if(f.op==5){assert(f.payload==std::vector<uint8_t>{0} || f.payload==std::vector<uint8_t>({0,0x5a}));fault=0;}
        else assert(false); // No unsafe config/continuous/shell opcode ever sent.
        reply.op=nak?0x82:0x81;
        reply.payload=nak?std::vector<uint8_t>{2,f.op}:f.op==1?std::vector<uint8_t>{0,46}:std::vector<uint8_t>{0};
        if(wrong_seq&&f.op==1)++reply.seq;
      }
      if(respond) {
        if(f.op==1 && late_ack)usleep(700000); // Past the 600ms request deadline.
        if(f.op==1 && fault_before_ack) {
          fault=10;std::vector<uint8_t> event;
          assert(aipet::mcu_encode({3,0x93,0,{10}},event));
          assert(write(master_fd,event.data(),event.size())==ssize_t(event.size()));
        }
        std::vector<uint8_t> wire;assert(aipet::mcu_encode(reply,wire));
        // Fragment every frame across header/payload/CRC boundaries.
        for(uint8_t b:wire){assert(write(master_fd,&b,1)==1);usleep(50);}
      }
    }
    usleep(1000);
  }
}
static int ctl(std::initializer_list<const char *> args)
{
  std::vector<char *> a;for(auto p:args)a.push_back(const_cast<char *>(p));
  return aipet::mcu_control(a.size(),a.data());
}
int main()
{
  int slave;assert(openpty(&master_fd,&slave,slave_name,nullptr,nullptr)==0);close(slave);
  assert(fcntl(master_fd,F_SETFL,O_NONBLOCK)==0);
  std::thread peripheral(peer);
  assert(ctl({"start"})==0);usleep(100000);
  assert(ctl({"ping"})==0);
  assert(ctl({"config-get"})==0);
  assert(ctl({"stepper-stat"})==0);
  assert(ctl({"link-stat"})==0);
  for(auto text:{"电机状态","链路状态","下位机配置"})
    assert(aipet::routed_ask(text,false,false)==0);
  assert(ctl({"servo","30"})==1 && servos==0);
  assert(ctl({"arm","session"})==0);
  assert(aipet::routed_ask("前进",true,false)==0 && moves==0);
  assert(aipet::routed_ask("前进",false,false)==0 && moves==1);
  unsigned before=moves;
  for(auto text:{"不要前进","前进然后左转","前进999999步","前进0步","前进65535步"})
    assert(aipet::routed_ask(text,false,false)==1 && moves==before);
  assert(ctl({"servo","46"})==1);
  asr_text="向左看";tts_pause_us=1200000;
  unsigned before_ping=pings;
  assert(command({"listen","100"})==0 && servos==1 && uploads==1 && plays>0);
  assert(pings>=before_ping+4);tts_pause_us=0;
  assert(command({"loop","999","1"})==1&&command({"loop","1000","21"})==1);
  asr_text="向右看";expected_asr_bytes=32000;
  assert(command({"loop","1000","2"})==0&&servos==3&&uploads==3);
  expected_asr_bytes=3200;
  aipet_mcu_tools_register();assert(provider&&executor);
  char *schema=provider();assert(schema&&std::strstr(schema,"stm32_control"));std::free(schema);
  char out[512];
  aipet::mcu_turn_begin("查询电机状态");
  assert(executor("stm32_control","{\"action\":\"stepper-stat\"}",out,sizeof(out))==0);
  assert(std::strstr(out,"\"ok\":true")&&std::strstr(out,"不是编码器实测"));
  assert(executor("stm32_control","{\"action\":\"forward\"}",out,sizeof(out))==0);
  assert(std::strstr(out,"\"ok\":false"));
  aipet::mcu_turn_end();
  assert(executor("stm32_control","{\"action\":\"forward\"}",out,sizeof(out))==0);
  assert(std::strstr(out,"\"ok\":false") && moves==before);
  use_tool=true;double_tool=true;
  assert(aipet::routed_ask("前进",false,true)==0 && moves==before+1);
  assert(last_spoken.find("不是动作完成")!=std::string::npos && last_spoken.find("模型声称")==std::string::npos);
  double_tool=false;bad_tool=true;before=moves;
  assert(aipet::routed_ask("前进",false,true)==0 && moves==before);bad_tool=false;
  assert(last_spoken.find("未确认任何动作")!=std::string::npos);
  aipet::mcu_turn_begin("前进");
  for(auto json:{"{\"action\":\"forward\",\"steps\":1.5}",
                 "{\"action\":\"forward\",\"action\":\"stop\"}",
                 "{\"action\":\"forward\",\"arm\":true}",
                 "{\"action\":\"clear-fault\"}"}) {
    assert(executor("stm32_control",json,out,sizeof(out))==0);
    assert(std::strstr(out,"\"ok\":false"));
  }
  aipet::mcu_turn_end();
  nak=true;
  assert(ctl({"move","forward","1","256"})==1 && moves==before+1);nak=false;
  usleep(100000);assert(moves==before+1); // A NAK is not retried.
  assert(ctl({"servo","0"})==1);assert(ctl({"arm"})==0);
  wrong_seq=true;
  assert(ctl({"move","forward","1","256"})==1 && moves==before+2);wrong_seq=false;
  assert(ctl({"servo","0"})==1); // ACK timeout disarms.
  assert(ctl({"arm","session"})==0);before=moves;late_ack=true;
  assert(ctl({"move","forward","1","256"})==1 && moves==before+1);
  late_ack=false;usleep(450000);
  assert(ctl({"ping"})==0 && ctl({"servo","0"})==1 && moves==before+1);
  assert(ctl({"arm","session"})==0);fault_before_ack=true;
  assert(ctl({"move","forward","1","256"})==1 && moves==before+2);
  fault_before_ack=false;usleep(100000);
  assert(ctl({"servo","0"})==1 && ctl({"arm"})==1);
  assert(ctl({"clear-fault","blind-ack"})==0 && ctl({"ping"})==0);
  assert(ctl({"arm"})==0);
  respond=false;usleep(900000);
  before=moves;assert(ctl({"move","forward","1","256"})==1 && moves==before);
  respond=true;usleep(400000);assert(ctl({"ping"})==0);
  assert(ctl({"servo","0"})==1); // Reconnected does NOT mean rearmed.
  fault=10;usleep(300000);assert(ctl({"arm"})==1);
  assert(ctl({"clear-fault","blind-ack"})==0);assert(ctl({"ping"})==0);assert(ctl({"arm"})==0);
  asr_text="停止";assert(command({"listen","100"})==0 && stops>0);
  assert(ctl({"servo","0"})==1);
  assert(aipet::uart_tts_control("on")==1 && aipet::uart_tts_control("test","text")==1);
  assert(command({"output","uart"})==1);
  assert(config_writes==0&&tts_frames==0&&tof_polls==0);
  assert(ctl({"config-set","4f"})==1&&config_writes==0); // explicit confirmation required
  assert(ctl({"config-set","80","confirm"})==1&&config_writes==0);
  assert(ctl({"arm"})==0);
  assert(ctl({"config-set","4f","confirm"})==0&&config_writes==1);
  assert(ctl({"ping"})==0&&ctl({"servo","0"})==1); // config writes never rearm
  assert(ctl({"config-get"})==0&&ctl({"tof-poll","50"})==0&&tof_polls==1);
  assert(ctl({"tts","测试"})==0&&tts_frames==1);
  // v225: actual route -> worker -> fragmented PTY ACK, not a mock permission.
  assert(ctl({"desktop","on"})==0);before=moves;
  std::string desktop_reply;int desktop_status=0;
  assert(aipet::mcu_route("前进十五部",false,desktop_reply,&desktop_status)&&!desktop_status);
  assert(moves==before+1&&last_move_pulses==300&&last_move_speed==2);
  assert(aipet::mcu_route("停止",false,desktop_reply,&desktop_status)&&!desktop_status);
  assert(aipet::mcu_route("前进三步",false,desktop_reply,&desktop_status)&&!desktop_status);
  assert(moves==before+2&&last_move_pulses==60);
  nak=true;
  assert(aipet::mcu_route("前进一步",false,desktop_reply,&desktop_status)&&desktop_status);
  nak=false;usleep(100000);unsigned inhibited_moves=moves;
  assert(aipet::mcu_route("前进一步",false,desktop_reply,&desktop_status)&&desktop_status);
  assert(moves==inhibited_moves); // Healthy heartbeat alone cannot revive a failed command.
  assert(ctl({"desktop","on"})==0);
  assert(aipet::mcu_route("前进两步",false,desktop_reply,&desktop_status)&&!desktop_status);
  assert(last_move_pulses==40&&moves==inhibited_moves+1&&config_writes==1);
  assert(ctl({"desktop","off"})==0);
  assert(aipet::mcu_route("前进两步",false,desktop_reply,&desktop_status)&&desktop_status);
  assert(ctl({"disconnect"})==0);usleep(100000);
  finish_peer=true;peripheral.join();close(master_fd);
  std::puts("PASS: late ACK and fault-before-ACK cannot revive motion; NAK/timeout disarm; no motion retry");
  std::puts("PASS: real application voice-route-UART-TTS pipeline with mocked cloud/PCM and PTY MCU; NOT board acceptance");
}
