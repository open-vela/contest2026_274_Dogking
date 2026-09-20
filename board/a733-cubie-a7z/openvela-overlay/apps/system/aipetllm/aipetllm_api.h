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
  unsigned max_tokens; /* 1..64 */
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
/* Process-wide controls shared by the NSH frontend and embedded clients.
 * The return value is zero on success.  Unload/stop fail safely while the
 * model is busy; status writes a bounded snapshot without exposing weights. */
int aipetllm_generation_control(int stop);
int aipetllm_cache_control(int unload);
#ifdef __cplusplus
}
#endif
#endif
