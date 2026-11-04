/* SPDX-License-Identifier: Apache-2.0 */
#ifndef AIPET_VOICE_KWS_H
#define AIPET_VOICE_KWS_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Bounded CPU search only. Callbacks/backends must provide REAL 263-way logits
 * for each requested decoder context. No allocation, hardware or networking.
 * Policy is experimental, not calibrated wake-word accuracy acceptance. */
#define AIPET_KWS_VOCAB 263
#define AIPET_KWS_WORDS 8
#define AIPET_KWS_PHONES 16
#define AIPET_KWS_PATHS 16
#define AIPET_KWS_CONTEXTS (AIPET_KWS_PATHS + 1)
struct aipet_kws_word {
  uint32_t count;
  int32_t tokens[AIPET_KWS_PHONES];
  float boost;
  float threshold; /* actual arithmetic mean acoustic-probability gate */
};
struct aipet_kws_config {
  uint32_t max_span;       /* encoder output frames, NOT fbank frames */
  uint32_t max_blank;
  uint32_t prefix_wait;    /* defer a terminal that prefixes a longer word */
  uint32_t cooldown;
  float start_floor;
  float strong_probability;
  uint32_t min_strong;
};
struct aipet_kws_hit {
  int32_t word;            /* -1 means no hit */
  uint32_t frame;
  float mean_probability;
  float relative_score;
};
/* Memory must be aligned to max_align_t. init rejects invalid policy/graphs.
 * contexts emits root + unique active two-token decoder contexts.
 * advance consumes rows in precisely that order; negative return leaves the
 * state unchanged (caller can cancel/retry safely). Single owner per instance.
 * Reset search between utterances; don't reset acoustic caches on every hit.
 */
size_t aipet_kws_bytes(void);
int aipet_kws_init(void *memory, size_t bytes,
                   const struct aipet_kws_word *words, size_t count,
                   const struct aipet_kws_config *config);
int aipet_kws_reset(void *memory);
int aipet_kws_contexts(const void *memory, int32_t *pairs, size_t capacity);
int aipet_kws_advance(void *memory, const float *logits, size_t rows,
                      struct aipet_kws_hit *hit);
#ifdef __cplusplus
}
#endif
#endif
