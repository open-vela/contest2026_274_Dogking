/* SPDX-License-Identifier: Apache-2.0 */
#include "local_llm.hxx"
#include "pet_routes.hxx"
#include "aipetllm_api.h"
#include "local_model_path.hxx"
#include "local_llm_history.hxx"
#include "pet_persona.hxx"
#include <nuttx/config.h>
#ifdef CONFIG_SYSTEM_A733STORAGE
#include <system/a733_storage.h>
#endif

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef AIPET_LOCAL_DIR
#define AIPET_LOCAL_DIR "/data/ai_agent"
#endif

namespace aipet {
namespace {
constexpr const char *disabled_path =
  AIPET_LOCAL_DIR "/config/local_llm.disabled";
constexpr const char *model_path =
  AIPET_LOCAL_DIR "/config/local_llm.model";
constexpr const char *tokens_path =
  AIPET_LOCAL_DIR "/config/local_llm.tokens";
constexpr const char *cores_path =
  AIPET_LOCAL_DIR "/config/local_llm.cores";
#ifdef CONFIG_A733_PRODUCT_V12
// Do not inherit v1.1's saved three-pair history on a kernel-only upgrade.
// Preserve that file for rollback; explicit v1.2 user choices remain supported.
constexpr const char *context_path = AIPET_LOCAL_DIR "/config/local_llm.context.v12";
constexpr unsigned default_context = 0;
#else
constexpr const char *context_path = AIPET_LOCAL_DIR "/config/local_llm.context";
constexpr unsigned default_context = 3;
#endif
constexpr const char *system_prompt = pet_persona;

pthread_mutex_t infer_lock = PTHREAD_MUTEX_INITIALIZER;
LocalHistory history;
char history_model[1024];
struct stat history_stat{};
struct InferLease { ~InferLease(){pthread_mutex_unlock(&infer_lock);} };

void clear_history()
{ history.clear();history_model[0]=0;history_stat={}; }

bool valid_model(const char *path,uint64_t expected)
{
  struct stat st;
  if(!path||stat(path,&st)||!S_ISREG(st.st_mode)||st.st_size<32||
     (expected&&(uint64_t)st.st_size!=expected))return false;
  unsigned char header[8];FILE *f=fopen(path,"rb");if(!f)return false;
  size_t n=fread(header,1,sizeof(header),f);int closed=fclose(f);
  return !closed&&n==sizeof(header)&&!memcmp(header,"GGUF",4)&&
    (header[4]==2||header[4]==3)&&!header[5]&&!header[6]&&!header[7];
}

bool read_line(const char *path, char *out, size_t capacity)
{
  if (!out || capacity < 2) return false;
  FILE *file = fopen(path, "rb");
  if (!file) return false;
  const bool ok = fgets(out, capacity, file) != nullptr;
  const bool overflow = ok && !strchr(out, '\n') && fgetc(file) != EOF;
  fclose(file);
  if (!ok || overflow) { out[0] = 0; return false; }
  out[strcspn(out, "\r\n")] = 0;
  return out[0] != 0;
}

int write_line(const char *path, const char *value)
{
  char temporary[160];
  if (!path || !value || strchr(value,'\n') || strchr(value,'\r'))
    return -EINVAL;
  int n=snprintf(temporary,sizeof(temporary),"%s.tmp",path);
  if(n<=0 || static_cast<size_t>(n)>=sizeof(temporary))return -ENAMETOOLONG;
  // Offline configuration must not depend on online/API-key setup.
  if(mkdir(AIPET_LOCAL_DIR,0700)<0 && errno!=EEXIST)return -errno;
  if(mkdir(AIPET_LOCAL_DIR "/config",0700)<0 && errno!=EEXIST)return -errno;
  int fd=open(temporary,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
  if(fd<0)return -errno;
  FILE *file = fdopen(fd,"wb");
  if (!file) {int error=errno;close(fd);unlink(temporary);return -error;}
  bool ok = fprintf(file, "%s\n", value) > 0 && fflush(file) == 0 && fsync(fd)==0;
  if (fclose(file) != 0) ok = false;
  if (!ok) { unlink(temporary); return -EIO; }
  if (rename(temporary, path) < 0) { int error = errno; unlink(temporary); return -error; }
  return 0;
}

void load_config(char *model, size_t capacity, unsigned &tokens, unsigned &cores,
                 const char *&policy)
{
  char value[1024],configured[1024]="auto";bool ready=false;
  if(read_line(model_path,value,sizeof(value)))memcpy(configured,value,strlen(value)+1);
#ifdef CONFIG_SYSTEM_A733STORAGE
  ready=a733_storage_ready();
#endif
  if(choose_local_model(configured,ready,valid_model,model,capacity,&policy))
    {model[0]=0;policy="invalid-config";}
  tokens = 128;
  if (read_line(tokens_path, value, sizeof(value))) {
    char *end = nullptr; unsigned long parsed = strtoul(value, &end, 10);
    if (end && !*end && parsed >= 1 && parsed <= 256) tokens = parsed;
  }
  cores = 0xfc;
  if (read_line(cores_path, value, sizeof(value))) {
    char *end = nullptr; unsigned long parsed = strtoul(value, &end, 16);
    if (end && !*end && parsed && parsed <= 0xff) cores = parsed;
  }
}

struct BufferSink {
  char *data; size_t capacity; size_t used;
  std::string previous;
  bool held=false, repeated=false;
};

unsigned context_limit()
{
  char value[16];unsigned limit=default_context;
  if(read_line(context_path,value,sizeof(value))) {
    char *end=nullptr;unsigned long n=strtoul(value,&end,10);
    if(end && end!=value && !*end && n<=AIPETLLM_HISTORY_LIMIT)limit=n;
  }
  return limit;
}
int append(void *opaque, const char *data, size_t size)
{
  auto *sink = static_cast<BufferSink *>(opaque);
  if (!sink || !data || size > sink->capacity - 1 - sink->used) return 1;
  memcpy(sink->data + sink->used, data, size);
  sink->used += size;
  sink->data[sink->used] = 0;
  if(sink->held) {
    size_t n=sink->used<sink->previous.size()?sink->used:sink->previous.size();
    if(!memcmp(sink->data,sink->previous.data(),n)) {
      if(n>=48 || n==sink->previous.size()) {
        sink->repeated=true;return -ESTALE;
      }
      return 0; // Do not say a replay prefix before the guard can decide.
    }
    sink->held=false;
    return local_route_stream(sink->data,sink->used);
  }
  return local_route_stream(data,size);
}

int infer_request(const char *text, char *reply, size_t capacity,unsigned limit=0)
{
  if (!text || !*text || !reply || capacity < 2) return -EINVAL;
  reply[0] = 0;
  if (pthread_mutex_trylock(&infer_lock) != 0) return -EBUSY;
  InferLease lease;
  if (access(disabled_path, F_OK) == 0) return -ENODEV;
  char model[1024]; unsigned tokens, cores;const char *policy;
  load_config(model, sizeof(model), tokens, cores,policy);
  // Old installations persisted 16/24/32 as a latency experiment. Those
  // values must not silently amputate a spoken reply in production.
  if(limit)tokens=limit;
  else if(tokens<128){printf("local-llm: legacy token cap %u -> 128 for complete replies\n",tokens);tokens=128;}
  if (!valid_model(model,0)) return -ENOENT;
  struct stat identity;
  if(stat(model,&identity))return -errno;
  if(strcmp(history_model,model) || history_stat.st_size!=identity.st_size ||
     history_stat.st_mtime!=identity.st_mtime || history_stat.st_ino!=identity.st_ino ||
     history_stat.st_dev!=identity.st_dev)clear_history();
  unsigned keep=limit?0:context_limit(); // Warmup never reads/commits dialogue.
#ifdef CONFIG_A733_PRODUCT_V12
  printf("local-latency-v220: history=%s pairs=%u; persona, resident weights and exact-prefix KV retained; full replies\n",
         keep?"opt-in":"off",keep);
#endif
  aipetllm_turn turns[AIPETLLM_HISTORY_LIMIT]{};
  unsigned count=keep?history.count:0,used=0;
  history.view(turns);
  printf("aipet-local: storage=%s model='%s'\n",policy,model);
  if(!limit)if(const char *identity_text=pet_identity(text)) {
    // Product identity is a known application fact, not model inference.
    size_t bytes=strlen(identity_text);if(bytes>=capacity)return -ENOSPC;
    int rc=local_route_stream(identity_text,bytes);if(rc)return rc;
    memcpy(reply,identity_text,bytes+1);
    if(keep && history.append(text,reply,keep)) {strcpy(history_model,model);history_stat=identity;}
    std::puts("local-persona: application identity response; no LLM inference or claimed token latency");
    return 0;
  }
  BufferSink sink{reply, capacity, 0, {}, false, false};
  if(!limit && count && strcmp(text,turns[count-1].user) &&
     !strstr(text,"重复") && !strstr(text,"再说") && !strstr(text,"再讲")) {
    sink.previous=turns[count-1].assistant;
    sink.held=sink.previous.size()>=24;
  }
  aipetllm_request request{model, system_prompt, text, tokens, cores,
                           append, &sink};
  aipetllm_metrics metrics{};
  int result = aipetllm_infer_chat(&request, turns, count, &used, &metrics);
  if(sink.repeated) {
    // One bounded, logged history-free regeneration; no alternate model,
    // canned answer or replay was sent to speech/UI/actions.
    std::puts("local-llm: repeated-answer guard; regenerate once without stale dialogue (weights retained)");
    int cleared=aipetllm_prefix_clear();if(cleared)return cleared;
    sink.used=0;sink.repeated=false;sink.held=true;reply[0]=0;used=0;
    result=aipetllm_infer_chat(&request,nullptr,0,&used,&metrics);
    if(sink.repeated){reply[0]=0;return -EBADMSG;}
  }
  if (result || !sink.used) { reply[0]=0;return result == 125 ? -ECANCELED :
      result == -ENOSPC ? -E2BIG : -EIO; }
  if(!limit && metrics.output_tokens==tokens) {
    // Already spoken complete clauses cannot be recalled. Do NOT flush an
    // unfinished tail to TTS or poison later turns with this partial pair.
    std::puts("local-llm: finish=length, reply INCOMPLETE; no history commit; pending speech discarded");
    reply[0]=0;return -EOVERFLOW;
  }
  if(sink.held && sink.used) {
    // A short, non-repeated completion must not be swallowed by lookahead.
    int rc=local_route_stream(reply,sink.used);if(rc){reply[0]=0;return rc;}
  }
  Reply checked;
  if(!parse_reply(reply,checked)){reply[0]=0;return -EILSEQ;}
  if(keep) {
    history.retain(used);
    if(strstr(reply,"<|") || strstr(reply,"<s>") || strstr(reply,"</s>") ||
       strstr(reply,"<think>") || strstr(reply,"</think>") ||
       !history.append(text,reply,keep))clear_history();
    else {strcpy(history_model,model);history_stat=identity;}
  }
  printf("local-context: used=%u dropped=%u stored=%u limit=%u RAM-only shared-device-session\n",
         used,count-used,history.count,context_limit());
  printf("aipet-local: prompt=%u generated=%u first=%.3fs rest=%.3fs cores=%02x\n",
         metrics.input_tokens, metrics.output_tokens,
         metrics.first_token_seconds, metrics.after_first_seconds, cores);
  return 0;
}
int infer(const char *text,char *reply,size_t capacity)
{ return infer_request(text,reply,capacity); }

int set_disabled(bool disabled)
{
  if (!disabled) {
    if (unlink(disabled_path) < 0 && errno != ENOENT) return -errno;
    return 0;
  }
  return write_line(disabled_path, "disabled");
}
}

void install_local_llm_backend() { set_local_route_backend(infer); }

int local_llm_preflight()
{
  if(pthread_mutex_trylock(&infer_lock))return -EBUSY;
  InferLease lease;char model[1024];unsigned tokens,cores;const char *policy;
  load_config(model,sizeof(model),tokens,cores,policy);int rc=0;struct stat st{};
  if(access(disabled_path,F_OK)==0)rc=-ENODEV;
  else if(!*model)rc=-EINVAL;
  else if(stat(model,&st)<0)rc=-errno;
  else if(!S_ISREG(st.st_mode) || st.st_size<32)rc=-EBADMSG;
  else if(access(model,R_OK)<0)rc=-errno;
  else if(!valid_model(model,0))rc=-EBADMSG;
  std::printf("llm-preflight-v215: model='%s' storage=%s bytes=%lld status=%d; header only, no model load\n",
    model,policy,static_cast<long long>(st.st_size),rc);
  if(rc==-ENOENT)std::puts("llm-preflight-v215: selected file is missing; inspect /data/models and /ufs/models, then 'aipet local model <actual.gguf>' (no download/copy performed)");
  std::fflush(stdout);return rc;
}

int local_llm_control(const char *command, const char *value)
{
  if(command && !strcmp(command,"check"))return local_llm_preflight()?1:0;
  if(command && !strcmp(command,"stop"))return aipetllm_generation_control(1);
  // preload/inference manages its own lease; do not hold it recursively.
  if(command && !strcmp(command,"preload")) {
    int checked=local_llm_preflight();
    if(checked){printf("local-llm: preload rejected (%d); no weights allocated\n",checked);return 1;}
    char discard[512];
    puts("local-llm: warming configured model; dialogue history unchanged");
    int result=infer_request("只回答：好",discard,sizeof(discard),1);
    printf("local-llm: preload %s (%d)\n",result?"failed":"ready",result);
    return result?1:0;
  }
  if(pthread_mutex_trylock(&infer_lock)) {puts("local-llm: busy");return 1;}
  InferLease lease;
  char model[1024]; unsigned tokens, cores;const char *policy;
  load_config(model, sizeof(model), tokens, cores,policy);
  if (!command || !strcmp(command, "status")) {
    printf("local-llm: %s model='%s' model-file=%s tokens=%u cores=%02x storage=%s\n",
           access(disabled_path, F_OK) == 0 ? "off" : "on",
           model, valid_model(model,0) ? "ready" : "missing", tokens, cores,policy);
    aipetllm_cache_control(0);
    printf("local-context: stored=%u limit=%u RAM-only shared-device-session; clear with 'aipet local clear'\n",
           history.count,context_limit());
#ifdef CONFIG_A733_PRODUCT_V12
    printf("local-profile-v220: v1.2 PREVIEW default-history=0 config=%s; old v1.1 config unchanged\n",context_path);
#endif
    return 0;
  }
  if (!strcmp(command, "on") || !strcmp(command, "off")) {
    int result = set_disabled(!strcmp(command, "off"));
    if(!result && !strcmp(command,"off"))clear_history();
    printf("local-llm: %s%s\n", command, result ? " failed" : "");
    return result ? 1 : 0;
  }
  if (!strcmp(command, "model") && value &&
      (value[0] == '/'||!strcmp(value,"auto")||!strcmp(value,"sd")) &&
      strlen(value) < sizeof(model) && !strstr(value, "..")) {
    int result = write_line(model_path, value);
    if(!result)clear_history();
    puts(result ? "local-llm: model save failed" :
                  "local-llm: model saved; unload before switching a resident model");
    return result ? 1 : 0;
  }
  if (!strcmp(command, "tokens") && value) {
    char *end = nullptr; unsigned long parsed = strtoul(value, &end, 10);
    if (!end || *end || parsed < 1 || parsed > 256) goto usage;
    return write_line(tokens_path, value) ? 1 : 0;
  }
  if (!strcmp(command,"batch") && value && (!strcmp(value,"1")||!strcmp(value,"4"))) {
    int rc=aipetllm_prefill_control(value[0]-'0');
    if(!rc)printf("local-llm: prefill width=%s (RAM-only; weights/context retained)\n",value);
    return rc?1:0;
  }
  if (!strcmp(command, "cores") && value) {
    char *end = nullptr; unsigned long parsed = strtoul(value, &end, 16);
    if (!end || *end || !parsed || parsed > 0xff) goto usage;
    return write_line(cores_path, value) ? 1 : 0;
  }
  if (!strcmp(command,"clear")) {
    int rc=aipetllm_prefix_clear();
    if(!rc){clear_history();puts("local-context: cleared; resident weights retained");}
    return rc;
  }
  if (!strcmp(command,"context") && value) {
    char *end=nullptr;unsigned long n=strtoul(value,&end,10);
    if(!end || end==value || *end || n>AIPETLLM_HISTORY_LIMIT)goto usage;
#ifndef CONFIG_A733_PRODUCT_V12
    if(aipetllm_prefix_clear())return 1;
#endif
    int rc=write_line(context_path,value);
    if(!rc){clear_history();printf("local-context: limit=%lu pairs; history cleared, weights retained\n",n);}
#ifdef CONFIG_A733_PRODUCT_V12
    if(!rc)puts("local-latency-v220: exact-prefix KV retained; use 'aipet local clear' only for an explicit cold-prefix reset");
#endif
    return rc?1:0;
  }
  if (!strcmp(command, "unload")) {
    int rc=aipetllm_cache_control(1);if(!rc)clear_history();return rc;
  }
usage:
  puts("usage: aipet local status|on|off|preload|unload|stop|clear\n"
       "       aipet local model auto|sd|/absolute/model.gguf\n"
       "       aipet local tokens 1..256 (dialogue floor 128) | cores fc | context 0..4 | batch 1|4 (RAM-only)");
  return 1;
}

int local_backend_ask(const char *text, char *reply, size_t capacity)
{
  return infer(text, reply, capacity);
}
}
