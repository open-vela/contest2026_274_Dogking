/* SPDX-License-Identifier: Apache-2.0 */
#include "pet_routes.hxx"
#include "pet_persona.hxx"
#include "agent_bridge.h"
#include "speech_output.hxx"
#include "audio_app.hxx"
#include "voice_local.hxx"
#include "voice_phrase_pipe.hxx"
#include "mcu_link.hxx"
#include "aipet_ui_ipc.h"
#include <cJSON.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <pthread.h>
#include <unistd.h>

namespace aipet {
static const char *const uart_tts_marker=
  "/data/ai_agent/config/uart_tts.enabled";
static pthread_mutex_t turn_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t backend_lock=PTHREAD_MUTEX_INITIALIZER;
static LocalRouteBackend local_backend=nullptr;
static pthread_mutex_t stream_lock=PTHREAD_MUTEX_INITIALIZER;
static int (*stream_sink)(void *,const char *,std::size_t)=nullptr;
static void *stream_cookie=nullptr;
static pthread_t stream_owner;
static void stream_target(int (*sink)(void *,const char *,std::size_t),void *cookie)
{
  pthread_mutex_lock(&stream_lock);
  stream_sink=sink;stream_cookie=cookie;stream_owner=pthread_self();
  pthread_mutex_unlock(&stream_lock);
}
int local_route_stream(const char *data,std::size_t bytes)
{
  pthread_mutex_lock(&stream_lock);
  int rc=stream_sink&&pthread_equal(stream_owner,pthread_self())?
    stream_sink(stream_cookie,data,bytes):0;
  pthread_mutex_unlock(&stream_lock);return rc;
}
void set_local_route_backend(LocalRouteBackend backend)
{
  pthread_mutex_lock(&backend_lock); local_backend=backend;
  pthread_mutex_unlock(&backend_lock);
}
Rules default_routes()
{
  Rules r;
  r.direct={
    {{"几点","时间","日期","星期几","几号","现在几点"},{"现在是{time}","现在{time}啦~"}},
    {{"你好","嗨","hello","hi","早上好","晚上好","下午好"},{"你好呀！今天想聊什么？","嗨！我是小派，你的桌面小伙伴~","喵~你好！"}},
    {{"再见","拜拜","晚安","bye","回头见"},{"拜拜，下次再聊！","晚安，好梦哟~","再见啦，我会想你的！"}},
    {{"谢谢","感谢","多谢","谢谢小派"},{"不客气！能帮到你我很开心~","嘻嘻，不用谢！"}},
    {{"天气","气温","下雨","刮风","多少度"},{"这个嘛…我暂时查不到天气，不过你可以看看窗外~"}}};
  r.local={{{"笑话","搞笑","逗我","幽默","段子"},{}},
           {{"你是谁","你叫什么","你的名字","你是什么"},{}}};
  return r;
}
static bool strings(cJSON *array,std::vector<std::string> &out,bool required)
{
  if (!array && !required) return true;
  if (!cJSON_IsArray(array) || cJSON_GetArraySize(array)>32) return false;
  cJSON *v;
  cJSON_ArrayForEach(v,array) {
    if (!cJSON_IsString(v) || !v->valuestring[0] || strlen(v->valuestring)>512) return false;
    Reply checked;
    if (!parse_reply(v->valuestring,checked)) return false;
    out.emplace_back(v->valuestring);
  }
  return !required || !out.empty();
}
static bool group(cJSON *obj,std::vector<Rule> &out,bool direct)
{
  if (!cJSON_IsObject(obj)) return false;
  cJSON *entry;
  cJSON_ArrayForEach(entry,obj) {
    if (entry->string && entry->string[0]=='_') continue;
    if (!cJSON_IsObject(entry) || out.size()>=64) return false;
    Rule r;
    if (!strings(cJSON_GetObjectItemCaseSensitive(entry,"keywords"),r.keywords,true) ||
        (direct && !strings(cJSON_GetObjectItemCaseSensitive(entry,"actions"),r.replies,false))) return false;
    out.emplace_back(std::move(r));
  }
  return true;
}
bool load_routes(const char *path,Rules &out)
{
  FILE *f=fopen(path,"rb");
  if (!f) return false;
  std::vector<char> storage(16385);
  char *data=storage.data();
  size_t n=fread(data,1,storage.size()-1,f);
  bool bad=ferror(f) || fgetc(f)!=EOF; fclose(f);
  if (bad || !n || memchr(data,0,n)) return false;
  data[n]=0;
  cJSON *root=cJSON_Parse(data);
  Rules r;
  bool ok=cJSON_IsObject(root) &&
    group(cJSON_GetObjectItemCaseSensitive(root,"direct_rules"),r.direct,true) &&
    group(cJSON_GetObjectItemCaseSensitive(root,"local_llm_rules"),r.local,false);
  cJSON_Delete(root);
  if (ok) out=std::move(r);
  return ok;
}
static void replace(std::string &s,const char *what,const char *value)
{
  size_t p=0;
  while ((p=s.find(what,p))!=std::string::npos) { s.replace(p,strlen(what),value); p+=strlen(value); }
}
std::string expand_clock_template(const std::string &text)
{
  time_t now=time(nullptr); struct tm tm;
  std::string out=text;
  if (!localtime_r(&now,&tm)) return {};
  char clock[16],date[48];
  strftime(clock,sizeof(clock),"%H:%M",&tm);
  strftime(date,sizeof(date),"%Y年%m月%d日",&tm);
  const char *days[]={"星期日","星期一","星期二","星期三","星期四","星期五","星期六"};
  replace(out,"{time}",clock); replace(out,"{date}",date); replace(out,"{weekday}",days[tm.tm_wday]);
  return out;
}
class BoardPorts : public Ports {
public:
  bool inspect=false,force=false,local_speech=false;
  bool streamed=false,stream_complete=false;
  bool speech_stream=false;
  VoicePhrasePipe *phrases=nullptr;
  std::string current_user;
  struct timespec stream_start{};
  BoardPorts() {clock_gettime(CLOCK_MONOTONIC,&stream_start);}
  static int chunk(void *cookie,const char *data,std::size_t bytes) {
    auto *self=static_cast<BoardPorts *>(cookie);
    if(!data||!bytes)return 0;
    if(!self->streamed) {
      struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
      long long ms=(now.tv_sec-self->stream_start.tv_sec)*1000LL+
        (now.tv_nsec-self->stream_start.tv_nsec)/1000000;
      printf("[local-stream first-visible=%lld ms; provisional text, no actions]\n",ms);
      self->streamed=true;
    }
    // Do not let generated terminal escapes/control bytes alter the console.
    for(std::size_t i=0;i<bytes;++i) {
      unsigned char c=static_cast<unsigned char>(data[i]);
      if((c>=32&&c!=127)||c=='\n'||c=='\t')if(putchar(c)==EOF)return 1;
    }
    if(fflush(stdout))return 1;
    return self->phrases?voice_phrase_accept(self->phrases,data,bytes):0;
  }
  int error=0;
  bool online() override {
    struct ifaddrs *list=nullptr; bool connected=false;
    if (!getifaddrs(&list)) {
      for (auto p=list;p;p=p->ifa_next)
        if (p->ifa_addr && p->ifa_name && !strcmp(p->ifa_name,"wlan0") &&
            p->ifa_addr->sa_family==AF_INET)
          connected=((sockaddr_in *)p->ifa_addr)->sin_addr.s_addr!=0;
      freeifaddrs(list);
    }
    return force || connected;
  }
  bool local_reply(const std::string &user,std::string &out) override {
    current_user=user;
    if (!inspect && !local_speech && mcu_turn_report(out)) return true;
    if (!inspect) (void)aipet_ui_notify("思考");
    if (inspect) { out="路由检查：local_llm（所选 GGUF，CPU2-7）"; return true; }
    if (error) printf("aipet: cloud unavailable error=%d; entering fallback\n",error);
    pthread_mutex_lock(&backend_lock); auto backend=local_backend;
    pthread_mutex_unlock(&backend_lock);
    if (!backend) { error=-ENOSYS; return false; }
    std::vector<char> reply(4097,0);
    stream_target(chunk,this);
    error=backend(user.c_str(),reply.data(),reply.size());
    stream_target(nullptr,nullptr);
    Reply validated;
    const bool valid=!error && strnlen(reply.data(),reply.size())<reply.size() &&
                     parse_reply(reply.data(),validated);
    if(streamed)puts(!valid?"\n[local-stream aborted; partial text is not a completed reply]":"\n[local-stream complete]");
    if (!valid) {if(!error)error=-EILSEQ;return false;}
    stream_complete=streamed;
    out=reply.data(); return true;
  }
  bool cloud_reply(const std::string &user,std::string &out) override {
    current_user=user;
    if (!inspect) (void)aipet_ui_notify("思考");
    if (inspect) { out="路由检查：cloud（官方 Agent）"; return true; }
    error=aipet_agent_submit(user.c_str(),90000);
    if (error) return false;
    std::vector<char> raw(4097,0);
    do { error=aipet_agent_poll(raw.data(),raw.size(),nullptr); if (error==-EAGAIN) usleep(10000); }
    while (error==-EAGAIN);
    if (error) return false;
    if (strnlen(raw.data(),raw.size())==raw.size()) { error=-EOVERFLOW; return false; }
    out=raw.data();
    if (!inspect) (void)mcu_turn_report(out); // Physical result overrides model's claims.
    return true;
  }
  size_t choose(size_t count) override {
    unsigned n=0; FILE *f=fopen("/dev/urandom","rb");
    if (f) { if (fread(&n,1,sizeof(n),f)!=sizeof(n)) n=0; fclose(f); }
    return count ? n%count : 0;
  }
  std::string expand_template(const std::string &s) override { return expand_clock_template(s); }
  bool present(const Reply &r) override {
    if(!stream_complete)printf("%s\n",r.text.c_str());
    const char *emotion=r.emotion=="普通"?pet_emotion(current_user,r.text):r.emotion.c_str();
    printf("[emotion=%s actions=%zu]\n",emotion,r.actions.size());
    if (!inspect) (void)aipet_ui_notify(emotion);
    if (!inspect && !speech_stream && !audio_muted() && audio_i2s_selected()) {
      const int result=local_speech?audio_i2s_local_speak(r.text.c_str()):audio_i2s_speak(r.text.c_str());
      if (result<0) printf("aipet: I2S TTS failed %d (text/UI retained; no implicit UART fallback)\n",result);
      if(local_speech && result<0){error=result;return false;}
    }
    return true;
  }
  /* The UI owns the ten-second hold timer, matching the Linux prototype. */
  void return_idle() override {}
};
int routed_ask(const char *text,bool inspect,bool force_cloud)
{
  Reply validate;
  if (!text || !parse_reply(text,validate)) { puts("aipet: invalid UTF-8 or oversized input"); return 1; }
  McuCommand control;int intent=mcu_intent(text,control);
  // STOP never waits for an LLM/network call or a busy conversation lease.
  if (!force_cloud || intent<0 || (intent==1&&control.op==12)) {
    std::string answer;int status=0;
    if(mcu_route(text,inspect,answer,&status)) {
      BoardPorts ports;ports.inspect=inspect;Reply reply;reply.text=answer;
      ports.present(reply);
      printf("[route=mcu_uart result=%d inspect=%u]\n",status,inspect);
      return status?1:0;
    }
  }
  if(pthread_mutex_trylock(&turn_lock)){puts("aipet: another conversation is active");return 1;}
  struct Lease { ~Lease(){mcu_turn_end();pthread_mutex_unlock(&turn_lock);} } lease;
  if(!inspect)mcu_turn_begin(text);
  Rules rules=default_routes();
  const char *path="/data/ai_agent/config/routes.json";
  if (!access(path,F_OK) && !load_routes(path,rules)) {
    puts("aipet: invalid routes.json; request not sent"); return 1;
  }
  if (force_cloud) { rules.direct.clear(); rules.local.clear(); }
  BoardPorts ports; ports.inspect=inspect; ports.force=force_cloud;
  Conversation conversation(rules,ports);
  struct timespec before,after; clock_gettime(CLOCK_MONOTONIC,&before);
  auto turn=conversation.run(text);
  clock_gettime(CLOCK_MONOTONIC,&after);
  long long ms=(after.tv_sec-before.tv_sec)*1000LL+(after.tv_nsec-before.tv_nsec)/1000000;
  printf("[route=%s fallback=%d elapsed=%lld ms]\n",
    turn.route==Route::direct?"local_rule":turn.route==Route::cloud?"cloud":"local_llm",
    turn.cloud_fallback,ms);
  if (!turn.ok) {
    printf("aipet: route failed %d\n",ports.error);
    if (ports.error==-ENOSYS) puts("Local LLM backend is not registered.");
    else if (ports.error==-ENOENT) puts("Local model is missing; copy the configured GGUF into /data/models.");
    else if (ports.error==-ENODEV) puts("Local LLM is disabled; use 'aipet local on'.");
  }
  return turn.ok?0:1;
}

int routed_local_ask(const char *text)
{
  Reply validate;
  if (!text || !parse_reply(text, validate)) {
    puts("aipet: invalid UTF-8 or oversized input"); return 1;
  }
  std::string answer;int result=0;
  if(mcu_route(text,false,answer,&result)) {
    BoardPorts ports;Reply r;r.text=answer;ports.present(r);return result?1:0;
  }
  if(pthread_mutex_trylock(&turn_lock)){puts("aipet: another conversation is active");return 1;}
  struct Lease { ~Lease(){mcu_turn_end();pthread_mutex_unlock(&turn_lock);} } lease;
  mcu_turn_begin(text);
  BoardPorts ports;
  std::string raw;
  struct timespec before, after;
  clock_gettime(CLOCK_MONOTONIC, &before);
  if (!ports.local_reply(text, raw)) {
    printf("aipet: local LLM failed %d\n", ports.error); return 1;
  }
  Reply reply;
  if (!parse_reply(raw, reply) || !ports.present(reply)) return 1;
  clock_gettime(CLOCK_MONOTONIC, &after);
  long long ms=(after.tv_sec-before.tv_sec)*1000LL+
               (after.tv_nsec-before.tv_nsec)/1000000;
  printf("[route=local_llm fallback=0 elapsed=%lld ms]\n", ms);
  return 0;
}

static int routed_offline_impl(const char *text,bool streaming)
{
  Reply checked;
  if(!text || !parse_reply(text,checked))return -EINVAL;
  if(streaming && (!(voice_local_capabilities()&2)||!audio_i2s_selected()||audio_muted()))return -ENODEV;
  if(pthread_mutex_trylock(&turn_lock))return -EBUSY;
  struct Lease { ~Lease(){pthread_mutex_unlock(&turn_lock);} } lease;
  BoardPorts ports;ports.local_speech=true;ports.speech_stream=streaming;
  VoicePhrasePipe *pipe=nullptr;
  if(streaming) {
    auto speak=[](void *,VoiceTextNext next,void *source,VoiceLocalCancel cancel,void *cookie){
      return audio_i2s_local_feed_cancel(next,source,cancel,cookie);};
    int rc=voice_phrase_open_feed(speak,nullptr,&pipe);if(rc)return rc;
    ports.phrases=pipe;
  }
  std::string raw;
  int result=ports.local_reply(text,raw)?0:ports.error?ports.error:-EIO;
  if(!result && !parse_reply(raw,checked))result=-EILSEQ;
  if(streaming) {
    // A non-streaming backend must not silently drop the whole response.
    if(!result && !ports.streamed)result=voice_phrase_accept(pipe,raw.data(),raw.size());
    VoicePhraseMetrics m{};int rc=voice_phrase_finish(pipe,result,&m);
    voice_phrase_close(pipe);ports.phrases=nullptr;
    std::printf("local-voice-pipeline: status=%d phrases=%u/%u first-phrase=%lld us first-synthesis-start=%lld us; PCM/device timing separate\n",
                rc,m.spoken,m.queued,m.first_phrase_us,m.first_synthesis_us);
    if(!result)result=rc;
  }
  if(result)return result;
  if(!ports.present(checked))return ports.error?ports.error:-EIO;
  std::puts("[route=local_llm speech=local fallback=0 actions=disabled]");
  return 0;
}
int routed_offline_ask(const char *text){return routed_offline_impl(text,false);}
int routed_offline_stream_ask(const char *text){return routed_offline_impl(text,true);}

int uart_tts_control(const char *command,const char *text)
{
  (void)text;
  if (!command || !strcmp(command,"status")) {
    puts("uart-tts: retired; UART4 reserved for STM32 at 115200. Use I2S speech output.");
    return 0;
  }
  if (!strcmp(command,"off")) {
    if (unlink(uart_tts_marker)<0 && errno!=ENOENT) {
      printf("aipet: disable UART TTS failed %d\n",errno); return 1;
    }
    puts("UART TTS disabled; UART4 MCU ownership unchanged."); return 0;
  }
  puts("UART TTS is disabled in this firmware; use 'aipet audio output i2s'. No UART bytes sent.");
  return 1;
}

}
