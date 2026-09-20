/* SPDX-License-Identifier: Apache-2.0 */
#include "local_llm.hxx"
#include "pet_routes.hxx"
#include "aipetllm_api.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

namespace aipet {
namespace {
constexpr const char *default_model =
  "/data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf";
constexpr const char *disabled_path =
  "/data/ai_agent/config/local_llm.disabled";
constexpr const char *model_path =
  "/data/ai_agent/config/local_llm.model";
constexpr const char *tokens_path =
  "/data/ai_agent/config/local_llm.tokens";
constexpr const char *cores_path =
  "/data/ai_agent/config/local_llm.cores";
constexpr const char *system_prompt =
  "你是运行在openvela设备上的AI桌宠小派。用自然、简短的中文回答，"
  "不要编造联网结果，不要输出Markdown；可以在开头使用一个情绪标签，"
  "例如[开心]、[思考]或[普通]。";

pthread_mutex_t infer_lock = PTHREAD_MUTEX_INITIALIZER;

bool regular_file(const char *path)
{
  struct stat state;
  return path && stat(path, &state) == 0 && S_ISREG(state.st_mode) &&
         state.st_size > 0;
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
  if (!path || !value || snprintf(temporary, sizeof(temporary), "%s.tmp", path) <= 0)
    return -EINVAL;
  FILE *file = fopen(temporary, "wb");
  if (!file) return -errno;
  bool ok = fprintf(file, "%s\n", value) > 0 && fflush(file) == 0;
  if (fclose(file) != 0) ok = false;
  if (!ok) { unlink(temporary); return -EIO; }
  if (rename(temporary, path) < 0) { int error = errno; unlink(temporary); return -error; }
  return 0;
}

void load_config(char *model, size_t capacity, unsigned &tokens, unsigned &cores)
{
  char value[1024];
  strncpy(model, default_model, capacity - 1);
  model[capacity - 1] = 0;
  if (read_line(model_path, value, sizeof(value)) && value[0] == '/' &&
      !strstr(value, "..")) {
    strncpy(model, value, capacity - 1); model[capacity - 1] = 0;
  }
  tokens = 32;
  if (read_line(tokens_path, value, sizeof(value))) {
    char *end = nullptr; unsigned long parsed = strtoul(value, &end, 10);
    if (end && !*end && parsed >= 1 && parsed <= 64) tokens = parsed;
  }
  cores = 0xfc;
  if (read_line(cores_path, value, sizeof(value))) {
    char *end = nullptr; unsigned long parsed = strtoul(value, &end, 16);
    if (end && !*end && parsed && parsed <= 0xff) cores = parsed;
  }
}

struct BufferSink { char *data; size_t capacity; size_t used; };
int append(void *opaque, const char *data, size_t size)
{
  auto *sink = static_cast<BufferSink *>(opaque);
  if (!sink || !data || size > sink->capacity - 1 - sink->used) return 1;
  memcpy(sink->data + sink->used, data, size);
  sink->used += size;
  sink->data[sink->used] = 0;
  return 0;
}

int infer(const char *text, char *reply, size_t capacity)
{
  if (!text || !*text || !reply || capacity < 2) return -EINVAL;
  if (access(disabled_path, F_OK) == 0) return -ENODEV;
  char model[1024]; unsigned tokens, cores;
  load_config(model, sizeof(model), tokens, cores);
  if (!regular_file(model)) return -ENOENT;
  if (pthread_mutex_trylock(&infer_lock) != 0) return -EBUSY;
  reply[0] = 0;
  BufferSink sink{reply, capacity, 0};
  aipetllm_request request{model, system_prompt, text, tokens, cores,
                           append, &sink};
  aipetllm_metrics metrics{};
  int result = aipetllm_infer(&request, &metrics);
  pthread_mutex_unlock(&infer_lock);
  if (result || !sink.used) return result == 125 ? -ECANCELED : -EIO;
  printf("aipet-local: prompt=%u generated=%u first=%.3fs rest=%.3fs cores=%02x\n",
         metrics.input_tokens, metrics.output_tokens,
         metrics.first_token_seconds, metrics.after_first_seconds, cores);
  return 0;
}

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

int local_llm_control(const char *command, const char *value)
{
  char model[1024]; unsigned tokens, cores;
  load_config(model, sizeof(model), tokens, cores);
  if (!command || !strcmp(command, "status")) {
    printf("local-llm: %s model='%s' model-file=%s tokens=%u cores=%02x\n",
           access(disabled_path, F_OK) == 0 ? "off" : "on",
           model, regular_file(model) ? "ready" : "missing", tokens, cores);
    aipetllm_cache_control(0);
    return 0;
  }
  if (!strcmp(command, "on") || !strcmp(command, "off")) {
    int result = set_disabled(!strcmp(command, "off"));
    printf("local-llm: %s%s\n", command, result ? " failed" : "");
    return result ? 1 : 0;
  }
  if (!strcmp(command, "model") && value && value[0] == '/' &&
      strlen(value) < sizeof(model) && !strstr(value, "..")) {
    int result = write_line(model_path, value);
    puts(result ? "local-llm: model save failed" :
                  "local-llm: model saved; unload before switching a resident model");
    return result ? 1 : 0;
  }
  if (!strcmp(command, "tokens") && value) {
    char *end = nullptr; unsigned long parsed = strtoul(value, &end, 10);
    if (!end || *end || parsed < 1 || parsed > 64) goto usage;
    return write_line(tokens_path, value) ? 1 : 0;
  }
  if (!strcmp(command, "cores") && value) {
    char *end = nullptr; unsigned long parsed = strtoul(value, &end, 16);
    if (!end || *end || !parsed || parsed > 0xff) goto usage;
    return write_line(cores_path, value) ? 1 : 0;
  }
  if (!strcmp(command, "stop")) return aipetllm_generation_control(1);
  if (!strcmp(command, "unload")) return aipetllm_cache_control(1);
  if (!strcmp(command, "preload")) {
    char discard[512];
    puts("local-llm: warming model; first SD load can take about three minutes");
    int result = infer("只回答：好", discard, sizeof(discard));
    printf("local-llm: preload %s (%d)\n", result ? "failed" : "ready", result);
    return result ? 1 : 0;
  }
usage:
  puts("usage: aipet local status|on|off|preload|unload|stop\n"
       "       aipet local model /data/models/model.gguf\n"
       "       aipet local tokens 1..64 | cores fc");
  return 1;
}

int local_backend_ask(const char *text, char *reply, size_t capacity)
{
  return infer(text, reply, capacity);
}
}
