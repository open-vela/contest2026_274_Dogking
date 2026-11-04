/* SPDX-License-Identifier: Apache-2.0 */
#include "aipetllm_api.h"
#include <cstdint>
#include <cerrno>
#include <exception>
#include <memory>
#include <new>
extern "C" {
#include "aipetllm_sequence.h"
int aipetllm_chat_system_tokens(const char *, const char *, const char *,
    std::uint32_t *, std::uint32_t, std::uint32_t *, std::uint32_t *);
void *aipetllm_stream_sink_open(const char *,
    int (*)(void *, const char *, std::size_t), void *);
int aipetllm_stream_emit(void *, std::uint32_t);
int aipetllm_stream_sink_finish(void *);
void aipetllm_stream_close(void *);
}

extern "C" int aipetllm_infer(const aipetllm_request *request,
                              aipetllm_metrics *metrics)
{
  return aipetllm_infer_chat(request, nullptr, 0, nullptr, metrics);
}

extern "C" int aipetllm_infer_chat(const aipetllm_request *request,
    const aipetllm_turn *history, unsigned turns, unsigned *used_turns,
    aipetllm_metrics *metrics)
{
  if (metrics != nullptr) *metrics = {};
  if (used_turns) *used_turns = 0;
  if (request == nullptr || request->model == nullptr ||
      request->system_prompt == nullptr || request->user_prompt == nullptr ||
      request->sink == nullptr || !request->cores || (request->cores & ~255u) ||
      !request->max_tokens || request->max_tokens > AIPETLLM_SEQUENCE_LIMIT ||
      turns > AIPETLLM_HISTORY_LIMIT || (turns && !history)) return 1;
  void *stream = nullptr;
  int result = 1;
#if defined(__cpp_exceptions)
  try
#endif
    {
      // Output budget grew for complete sentences; do not consume the NSH
      // thread's small stack with the enlarged prompt/output arrays.
      std::unique_ptr<aipetllm_sequence_s> storage(new(std::nothrow) aipetllm_sequence_s{});
      if(!storage)return -ENOMEM;
      auto &sequence=*storage;
      sequence.generate_limit = request->max_tokens;
      sequence.eos = 151645;
      sequence.chat_mode = 1;
      unsigned dropped = 0;
      do {
        result = aipetllm_chat_history_tokens(request->model, request->system_prompt,
          request->user_prompt, history ? history + dropped : nullptr, turns - dropped,
          sequence.input, AIPETLLM_INPUT_LIMIT, &sequence.input_count, &sequence.chat_stop);
        if (result != -ENOSPC || dropped == turns) break;
        ++dropped;
      } while (true);
      if (result) return result;
      stream = aipetllm_stream_sink_open(request->model, request->sink, request->context);
      if (stream == nullptr) return 1;
      sequence.emit = aipetllm_stream_emit;
      sequence.emit_context = stream;
      result = aipetllm_sequence_fast_checkpoint(request->model, &sequence, request->cores);
      if (!result) result = aipetllm_stream_sink_finish(stream);
      if (!result && used_turns) *used_turns = turns - dropped;
      if (metrics != nullptr)
        *metrics = {sequence.input_count, sequence.output_count,
                    sequence.first_token_seconds, sequence.after_first_seconds};
    }
#if defined(__cpp_exceptions)
  catch (const std::exception &) { result = 1; }
#endif
  aipetllm_stream_close(stream);
  return result;
}
