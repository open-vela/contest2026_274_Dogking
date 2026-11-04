#include "pet_routes.hxx"
#include "mcu_link.hxx"
#include "voice_local.hxx"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <unistd.h>
#include <cstring>
#include <atomic>
extern "C" int aipet_agent_submit(const char *,uint32_t) { return -1; }
extern "C" int aipet_agent_poll(char *,size_t,uint64_t *) { return -1; }
namespace aipet {
static bool local_audio_enabled=false;
static unsigned local_audio_calls=0,cloud_audio_calls=0;
static std::atomic<unsigned> stream_audio_calls{0};
static int stream_audio_error=0;
int mcu_intent(const std::string &,McuCommand &) { return 0; }
bool mcu_route(const char *,bool,std::string &,int *) { return false; }
void mcu_turn_begin(const char *) {}
void mcu_turn_end() {}
bool mcu_turn_report(std::string &) { return false; }
bool audio_muted() { return false; }
bool audio_i2s_selected() { return local_audio_enabled; }
int audio_i2s_speak(const char *) { ++cloud_audio_calls;return -1; }
int audio_i2s_local_speak(const char *) { ++local_audio_calls;return -ENODEV; }
unsigned voice_local_capabilities(){return local_audio_enabled?2:0;}
int audio_i2s_local_stream_speak_cancel(const char *text,int (*cancel)(void *),void *cookie){
  assert(text && *text);++stream_audio_calls;
  if(cancel && cancel(cookie))return -ECANCELED;
  return stream_audio_error;
}
int audio_i2s_local_feed_cancel(VoiceTextNext next,void *source,VoiceLocalCancel cancel,void *cookie) {
  char clause[769];int rc;
  for(;;) {
    rc=next(source,clause,sizeof(clause));
    if(rc==-EAGAIN){usleep(1000);continue;}
    if(rc<=0)break;
    int result=audio_i2s_local_stream_speak_cancel(clause,cancel,cookie);
    if(result)return result;
  }
  return rc;
}
}
int main(int argc,char **argv)
{
  auto r=aipet::default_routes();
  assert(r.direct.size()==5 && r.local.size()==2);
  assert(r.direct[0].keywords[0]=="几点");
  auto clock=aipet::expand_clock_template("{time}/{date}/{weekday}");
  assert(clock.find('{')==std::string::npos);
  assert(argc==3);
  assert(aipet::load_routes(argv[1],r));
  assert(r.direct.size()==1 && r.direct[0].replies[0]=="test {weekday}");
  assert(!aipet::load_routes(argv[2],r));
  assert(r.direct.size()==1); /* Failure does not replace the working rules. */
  assert(aipet::routed_ask("HELLO",false,false)==0);
  assert(aipet::routed_ask("写一个故事",false,true)==1);
  aipet::set_local_route_backend([](const char *,char *out,size_t cap) {
    const std::string reply="测试本地接口[开心]";
    if (cap<=reply.size()) return -1;
    reply.copy(out,reply.size()); out[reply.size()]=0; return 0;
  });
  assert(aipet::routed_ask("写一个故事",false,true)==0);
  aipet::local_audio_enabled=true;
  assert(aipet::routed_offline_ask("写一个故事")==-ENODEV);
  assert(aipet::local_audio_calls==1 && aipet::cloud_audio_calls==0);
  aipet::local_audio_enabled=false;
  FILE *captured=tmpfile();assert(captured);
  int saved=dup(STDOUT_FILENO);assert(saved>=0);
  fflush(stdout);assert(dup2(fileno(captured),STDOUT_FILENO)>=0);
  auto stream_backend=[](const char *,char *out,size_t cap) {
    std::thread other([]{assert(!aipet::local_route_stream("FOREIGN",7));});other.join();
    assert(!aipet::local_route_stream("流式",6));
    assert(!aipet::local_route_stream("测试",6));
    assert(cap>12);memcpy(out,"流式测试",13);return 0;
  };
  aipet::set_local_route_backend(stream_backend);
  assert(!aipet::routed_ask("写一个故事",false,true));
  assert(!aipet::local_route_stream("OUTSIDE",7));
  aipet::set_local_route_backend([](const char *,char *,size_t) {
    assert(!aipet::local_route_stream("部分文本",12));return -5;
  });
  assert(aipet::routed_ask("写一个故事",false,true)==1);
  assert(!aipet::local_route_stream("OUTSIDE2",8));
  fflush(stdout);assert(dup2(saved,STDOUT_FILENO)>=0);close(saved);
  rewind(captured);char buffer[4096]{};size_t n=fread(buffer,1,sizeof(buffer)-1,captured);fclose(captured);
  std::string transcript(buffer,n);
  auto pos=transcript.find("流式测试");assert(pos!=std::string::npos);
  assert(transcript.find("流式测试",pos+1)==std::string::npos);
  assert(transcript.find("FOREIGN")==std::string::npos && transcript.find("OUTSIDE")==std::string::npos);
  assert(transcript.find("first-visible=")!=std::string::npos &&
         transcript.find("local-stream complete")!=std::string::npos);
  assert(transcript.find("local-stream aborted")!=std::string::npos);
  assert(aipet::routed_offline_stream_ask("测试")==-ENODEV);
  aipet::local_audio_enabled=true;
  aipet::set_local_route_backend([](const char *,char *out,size_t cap){
    const char *text="你好。";assert(cap>strlen(text));
    assert(!aipet::local_route_stream(text,strlen(text)));
    for(unsigned i=0;i<100 && !aipet::stream_audio_calls;i++)usleep(1000);
    assert(aipet::stream_audio_calls==1); // synthesis before LLM returns
    strcpy(out,text);return 0;
  });
  assert(!aipet::routed_offline_stream_ask("测试"));
  assert(aipet::stream_audio_calls==1 && aipet::cloud_audio_calls==0);
  aipet::stream_audio_error=-EPIPE;
  aipet::set_local_route_backend([](const char *,char *out,size_t cap){
    assert(cap>6);strcpy(out,"你好");return 0;
  });
  assert(aipet::routed_offline_stream_ask("测试")==-EPIPE);
  assert(aipet::stream_audio_calls==2 && aipet::cloud_audio_calls==0);
  aipet::set_local_route_backend(nullptr);
  puts("PASS: provisional same-thread streaming, no duplicate final body, no foreign-thread leak");
  puts("bounded ordered routes/config/template tests passed");
}
