/* SPDX-License-Identifier: Apache-2.0 */
#include "pet_routes.hxx"
#include "agent_bridge.h"
#include "speech_output.hxx"
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
static UartTts uart_tts("/dev/ttyS4");
static UartSpeechOutput uart_speech(uart_tts);
static pthread_mutex_t backend_lock=PTHREAD_MUTEX_INITIALIZER;
static LocalRouteBackend local_backend=nullptr;
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
  bool inspect=false,force=false;
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
    if (inspect) { out="路由检查：local_llm（Qwen2.5，CPU2-7）"; return true; }
    if (error) printf("aipet: cloud unavailable error=%d; entering fallback\n",error);
    pthread_mutex_lock(&backend_lock); auto backend=local_backend;
    pthread_mutex_unlock(&backend_lock);
    if (!backend) { error=-ENOSYS; return false; }
    char reply[4097]={0};
    error=backend(user.c_str(),reply,sizeof(reply));
    if (error || strnlen(reply,sizeof(reply))==sizeof(reply)) return false;
    out=reply; return true;
  }
  bool cloud_reply(const std::string &user,std::string &out) override {
    if (inspect) { out="路由检查：cloud（官方 Agent）"; return true; }
    error=aipet_agent_submit(user.c_str(),90000);
    if (error) return false;
    char raw[4097];
    do { error=aipet_agent_poll(raw,sizeof(raw),nullptr); if (error==-EAGAIN) usleep(10000); }
    while (error==-EAGAIN);
    if (error) return false;
    out=raw; return true;
  }
  size_t choose(size_t count) override {
    unsigned n=0; FILE *f=fopen("/dev/urandom","rb");
    if (f) { if (fread(&n,1,sizeof(n),f)!=sizeof(n)) n=0; fclose(f); }
    return count ? n%count : 0;
  }
  std::string expand_template(const std::string &s) override { return expand_clock_template(s); }
  bool present(const Reply &r) override {
    printf("%s\n[emotion=%s actions=%zu]\n",r.text.c_str(),r.emotion.c_str(),r.actions.size());
    if (!inspect && access(uart_tts_marker,F_OK)==0) {
      const int result=uart_speech.speak(r.text);
      if (result<0) printf("aipet: UART TTS failed %d\n",result);
    }
    return true;
  }
  void return_idle() override {}
};
int routed_ask(const char *text,bool inspect,bool force_cloud)
{
  Rules rules=default_routes();
  const char *path="/data/ai_agent/config/routes.json";
  if (!access(path,F_OK) && !load_routes(path,rules)) {
    puts("aipet: invalid routes.json; request not sent"); return 1;
  }
  Reply validate;
  if (!text || !parse_reply(text,validate)) { puts("aipet: invalid UTF-8 or oversized input"); return 1; }
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

int uart_tts_control(const char *command,const char *text)
{
  if (!command || !strcmp(command,"status")) {
    const auto &s=uart_tts.status();
    printf("uart-tts: %s device=/dev/ttyS4 baud=9600 encoding=utf8 "
           "node=%s initialized=%d frames=%u failures=%u last=%d stage=%s\n",
           access(uart_tts_marker,F_OK)==0?"on":"off",
           access("/dev/ttyS4",F_OK)==0?"present":"missing",s.initialized,
           s.frames,s.failures,s.last_errno,s.stage);
    return 0;
  }
  if (!strcmp(command,"on")) {
    FILE *f=fopen(uart_tts_marker,"wb");
    if (!f) { printf("aipet: enable UART TTS failed %d\n",errno); return 1; }
    fputs("enabled\n",f); fclose(f);
    puts("UART TTS enabled; Agent replies will be spoken on /dev/ttyS4.");
    return 0;
  }
  if (!strcmp(command,"off")) {
    if (unlink(uart_tts_marker)<0 && errno!=ENOENT) {
      printf("aipet: disable UART TTS failed %d\n",errno); return 1;
    }
    uart_tts.reset(); puts("UART TTS disabled."); return 0;
  }
  if (!strcmp(command,"test") && text && *text) {
    int result=uart_speech.speak(text);
    printf("uart-tts test: %s (%d)\n",result==0?"frame sent":"failed",result);
    return result==0?0:1;
  }
  puts("usage: aipet tts status|on|off|test \"text\"");
  return 1;
}

}
