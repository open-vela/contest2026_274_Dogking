/* SPDX-License-Identifier: Apache-2.0 */
#include "mcu_link.hxx"
#include "mcu_tool_bridge.h"
#include <cJSON.h>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <time.h>
namespace aipet {
static pthread_mutex_t scope_lock=PTHREAD_MUTEX_INITIALIZER;
static bool active=false,used=false;
static int intent=0;
static McuCommand permit;
static std::string last_report;
static uint64_t expires=0;
static uint64_t clock_ms(){timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);return uint64_t(t.tv_sec)*1000+uint64_t(t.tv_nsec)/1000000;}
void mcu_turn_begin(const char *text)
{
  pthread_mutex_lock(&scope_lock);intent=mcu_intent(text?text:"",permit);
  active=true;used=false;last_report="Agent 未调用获授权的控制工具，未确认任何动作。";
  expires=clock_ms()+95000;pthread_mutex_unlock(&scope_lock);
}
void mcu_turn_end(){pthread_mutex_lock(&scope_lock);active=false;pthread_mutex_unlock(&scope_lock);}
bool mcu_turn_used(){pthread_mutex_lock(&scope_lock);bool value=used;pthread_mutex_unlock(&scope_lock);return value;}
bool mcu_turn_report(std::string &out)
{
  pthread_mutex_lock(&scope_lock);
  bool relevant=active&&intent==1&&(permit.op==1||permit.op==2);
  if(relevant)out=last_report;
  pthread_mutex_unlock(&scope_lock);return relevant;
}
static bool integer(cJSON *root,const char *name,int low,int high,int fallback,int &out)
{
  cJSON *v=cJSON_GetObjectItemCaseSensitive(root,name);out=fallback;
  if(!v)return true;
  if(!cJSON_IsNumber(v)||!std::isfinite(v->valuedouble)||v->valuedouble<low||v->valuedouble>high||std::floor(v->valuedouble)!=v->valuedouble)return false;
  out=int(v->valuedouble);return true;
}
static int execute(const char *name,const char *input,char *out,size_t cap)
{
  if(!name||std::strcmp(name,"stm32_control"))return -1;
  if(!out||cap<256)return -1;
  int rc=-EINVAL;std::string reply="参数无效，未发送。";
  cJSON *root=input&&strnlen(input,1025)<=1024?cJSON_ParseWithOpts(input,nullptr,true):nullptr;
  bool valid=cJSON_IsObject(root);unsigned seen=0;
  cJSON *item;
  cJSON_ArrayForEach(item,root) {
    const char *fields[]={"action","steps","speed","angle","duration_ms"};unsigned bit=0;
    for(unsigned i=0;i<5;i++)if(item->string&&!std::strcmp(item->string,fields[i]))bit=1u<<i;
    if(!bit||(seen&bit))valid=false;
    seen|=bit;
  }
  cJSON *action=root?cJSON_GetObjectItemCaseSensitive(root,"action"):nullptr;
  valid=valid&&cJSON_IsString(action);
  McuCommand c;
  if(valid) {
    int speed=2,steps=mcu_default_pulses,angle=0,ms=500;
    valid=integer(root,"speed",0,3,2,speed)&&integer(root,"steps",1,mcu_step_max,mcu_default_pulses,steps)&&
          integer(root,"angle",-45,45,0,angle)&&integer(root,"duration_ms",100,2000,500,ms);
    const char *a=action->valuestring;
    if(!std::strcmp(a,"status")) {c={6,{}};valid=valid&&seen==1;}
    else if(!std::strcmp(a,"config-get")) {c={8,{}};valid=valid&&seen==1;}
    else if(!std::strcmp(a,"stepper-stat")) {c={9,{}};valid=valid&&seen==1;}
    else if(!std::strcmp(a,"link-stat")) {c={10,{}};valid=valid&&seen==1;}
    else if(!std::strcmp(a,"stop")) {c={12,{}};valid=valid&&seen==1;}
    else if(!std::strcmp(a,"servo"))valid=valid&&!(seen&6)&&mcu_servo(angle,ms,c);
    else {
      unsigned direction=4;const char *names[]={"forward","backward","left","right"};
      for(unsigned i=0;i<4;i++)if(!std::strcmp(a,names[i]))direction=i;
      valid=valid&&!(seen&24)&&mcu_move(direction,speed,(seen&2)?unsigned(steps)*mcu_step_scale:unsigned(steps),c);
    }
  }
  cJSON_Delete(root);
  if(valid) {
    pthread_mutex_lock(&scope_lock);
    // No arm, fault clear, config writes, calibrations or TTS opcode exposed.
    bool allowed=active&&clock_ms()<expires&&!used&&
      (c.op==6||(intent==1&&mcu_same(c,permit)));
    if(allowed)used=true; // Consume BEFORE sending, even on ACK timeout. Never retry motion.
    pthread_mutex_unlock(&scope_lock);
    if(allowed) {
      rc=mcu_request(c,reply);
      pthread_mutex_lock(&scope_lock);last_report=reply;pthread_mutex_unlock(&scope_lock);
    }
    else {rc=-EACCES;reply="没有本轮用户原文授权，或本轮命令已使用；未发送。";}
  }
  cJSON *answer=cJSON_CreateObject();
  if(!answer)return -1;
  cJSON_AddBoolToObject(answer,"ok",rc==0);
  cJSON_AddBoolToObject(answer,"command_acknowledged",rc==0);
  cJSON_AddBoolToObject(answer,"motion_complete",false);
  cJSON_AddNumberToObject(answer,"error",rc);
  cJSON_AddStringToObject(answer,"message",reply.c_str());
  char *json=cJSON_PrintUnformatted(answer);cJSON_Delete(answer);
  if(!json)return -1;
  size_t n=std::strlen(json);
  if(n>=cap){std::free(json);return -1;}
  std::memcpy(out,json,n+1);std::free(json);return 0;
}
static char *tools()
{
  const char *schema="[{\"name\":\"stm32_control\",\"description\":\"Control the UART STM32 ONLY when the original user explicitly requests this exact bounded action. Never arm or clear faults. ACK means accepted, NOT completed. Report errors honestly.\","
    "\"input_schema\":{\"type\":\"object\",\"additionalProperties\":false,\"properties\":{"
    "\"action\":{\"type\":\"string\",\"enum\":[\"status\",\"config-get\",\"stepper-stat\",\"link-stat\",\"stop\",\"forward\",\"backward\",\"left\",\"right\",\"servo\"]},"
    "\"steps\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":512,\"description\":\"Explicit human count, multiplied by 20 into bounded pulses. Omit when human gave no count; omitted means 512 raw pulses.\"},"
    "\"speed\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":3,\"default\":2},"
    "\"angle\":{\"type\":\"integer\",\"minimum\":-45,\"maximum\":45},"
    "\"duration_ms\":{\"type\":\"integer\",\"minimum\":100,\"maximum\":2000,\"default\":500}},\"required\":[\"action\"]}}]";
  return strdup(schema);
}
}
extern "C" void aipet_mcu_tools_register(void)
{
  static bool registered=false;
  if(registered)return;
  aipet_mcu_register_provider(aipet::tools,aipet::execute);registered=true;
}
