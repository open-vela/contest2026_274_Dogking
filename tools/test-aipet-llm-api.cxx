/* SPDX-License-Identifier: Apache-2.0 */
#include "../board/a733-cubie-a7z/openvela-overlay/apps/system/aipetllm/aipetllm_api.h"
extern "C" {
#include "../board/a733-cubie-a7z/openvela-overlay/apps/system/aipetllm/aipetllm_sequence.h"
}
#include <cassert>
#include <cstring>
#include <stdexcept>
#include <iostream>
#include <cerrno>
static int closes, mode, seen_turns, token_calls;
static int (*callback)(void *, const char *, size_t);
static void *cookie;
extern "C" int aipetllm_chat_history_tokens(const char *, const char *system,
  const char *,const aipetllm_turn *h,unsigned turns,
  uint32_t *tokens, uint32_t capacity, uint32_t *count, uint32_t *stop)
{
  ++token_calls;seen_turns=turns;
  assert(std::strcmp(system,"桌宠提示词") == 0 && capacity == 512);
  if(turns)assert(h && !std::strcmp(h[turns-1].assistant,"答"));
  if(mode==3 && turns>1)return -ENOSPC;
  if(mode==4)return -EINVAL;
  tokens[0] = 9707; *count = 1; *stop = 151645; return 0;
}
extern "C" void *aipetllm_stream_sink_open(const char *,
    int (*sink)(void *, const char *, size_t), void *context)
{ callback = sink; cookie = context; return &closes; }
extern "C" int aipetllm_stream_emit(void *, uint32_t)
{ return callback(cookie,"你好",6); }
extern "C" int aipetllm_stream_sink_finish(void *) { return mode == 1; }
extern "C" void aipetllm_stream_close(void *stream) { if (stream) ++closes; }
extern "C" int aipetllm_sequence_fast_checkpoint(const char *,
    aipetllm_sequence_s *sequence, unsigned mask)
{
  assert(mask == 0xfc && sequence->chat_mode == 1);
  if (mode == 2) throw std::runtime_error("failure");
  sequence->output_count = 1;
  return sequence->emit(sequence->emit_context,123);
}
static int output(void *context, const char *bytes, size_t size)
{ static_cast<std::string *>(context)->append(bytes,size); return 0; }
int main()
{
  std::string text;
  aipetllm_metrics metrics{};
  aipetllm_request request{"model","桌宠提示词","你好",24,0xfc,output,&text};
  assert(aipetllm_infer(&request,&metrics) == 0 && text == "你好");
  assert(metrics.input_tokens == 1 && metrics.output_tokens == 1 && closes == 1);
  mode = 1; assert(aipetllm_infer(&request,nullptr) == 1 && closes == 2);
  mode = 2; assert(aipetllm_infer(&request,nullptr) == 1 && closes == 3);
  request.cores = 0; assert(aipetllm_infer(&request,nullptr) == 1 && closes == 3);
  request.cores=0xfc;mode=3;unsigned used=99;
  aipetllm_turn history[3]={{"A","答"},{"B","答"},{"C","答"}};
  token_calls=0;
  assert(!aipetllm_infer_chat(&request,history,3,&used,&metrics));
  assert(used==1 && seen_turns==1 && token_calls==3 && closes==4);
  mode=4;token_calls=0;
  assert(aipetllm_infer_chat(&request,history,3,&used,&metrics)==-EINVAL);
  assert(used==0 && token_calls==1 && closes==4); // Invalid input is never pruned/retried.
  assert(aipetllm_infer_chat(&request,nullptr,1,&used,&metrics));
  mode=0;request.max_tokens=256;
  assert(!aipetllm_infer(&request,&metrics));
  request.max_tokens=257;assert(aipetllm_infer(&request,&metrics));
  std::cout << "LLM API validation and cleanup tests passed (mock backend)\n";
}
