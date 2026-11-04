/* SPDX-License-Identifier: Apache-2.0 */
#ifndef AIPETLLM_API_H
#define AIPETLLM_API_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Single synchronous request. Shares model cache and worker pool with llm.
 * Sink receives complete UTF-8 units, returns nonzero to abort cooperatively.
 * Generated output is provisional until a successful return; do not execute
 * actuator tags directly from this callback. No shell or stdout capture. */
struct aipetllm_request
{
  const char *model;
  const char *system_prompt;
  const char *user_prompt;
  unsigned max_tokens; /* 1..256; exhaustion is NOT natural EOS */
  unsigned cores;      /* nonzero subset of ff; default policy fc */
  int (*sink)(void *, const char *, size_t);
  void *context;
};
struct aipetllm_metrics
{
  unsigned input_tokens;
  unsigned output_tokens;
  double first_token_seconds;
  double after_first_seconds;
};
int aipetllm_infer(const struct aipetllm_request *, struct aipetllm_metrics *);
/* Bounded completed user/assistant pairs, oldest first. No tool roles and no
 * implicit global conversation. The caller owns session isolation/lifetime.
 * Drops oldest pairs ONLY on token-capacity overflow, before any model run.
 * used_turns is published on success; the legacy API stays single-turn. */
#define AIPETLLM_HISTORY_LIMIT 4
struct aipetllm_turn { const char *user; const char *assistant; };
int aipetllm_chat_history_tokens(const char *model, const char *system,
    const char *user, const struct aipetllm_turn *, unsigned turns,
    unsigned *output, unsigned capacity, unsigned *count, unsigned *stop);
int aipetllm_infer_chat(const struct aipetllm_request *,
    const struct aipetllm_turn *, unsigned turns, unsigned *used_turns,
    struct aipetllm_metrics *);
/* Clear reusable KV, not resident weights. Fails if model is busy. */
int aipetllm_prefix_clear(void);
/* 1 = serial reference, 4 = bounded row-reuse prefill; RAM policy, busy fails. */
int aipetllm_prefill_control(unsigned width);
/* Process-wide controls shared by the NSH frontend and embedded clients.
 * The return value is zero on success.  Unload/stop fail safely while the
 * model is busy; status writes a bounded snapshot without exposing weights. */
int aipetllm_generation_control(int stop);
int aipetllm_cache_control(int unload);
#ifdef __cplusplus
}
#endif
#endif
