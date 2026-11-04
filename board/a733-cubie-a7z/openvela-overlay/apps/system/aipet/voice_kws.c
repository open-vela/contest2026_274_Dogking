/* SPDX-License-Identifier: Apache-2.0 */
#include "voice_kws.h"
#include <errno.h>
#include <math.h>
#include <stdalign.h>
#include <string.h>

#define MAGIC UINT32_C(0x4b575331)
#define CANDIDATES (AIPET_KWS_PATHS * 2 + AIPET_KWS_WORDS)
struct path {
  int32_t context[2];
  uint32_t word, used, start, last, strong;
  float score, probability;
};
struct search {
  uint32_t magic, words, frame, count, cooldown_until;
  struct aipet_kws_config config;
  struct aipet_kws_word word[AIPET_KWS_WORDS];
  struct path paths[AIPET_KWS_PATHS];
};
static int valid(const struct search *s) { return s && s->magic == MAGIC; }
size_t aipet_kws_bytes(void) { return sizeof(struct search); }

int aipet_kws_init(void *memory, size_t bytes,
                   const struct aipet_kws_word *words, size_t count,
                   const struct aipet_kws_config *c)
{
  struct search *s = memory;
  size_t i, j;
  if (!memory || (uintptr_t)memory % alignof(struct search) ||
      bytes < sizeof(*s) || !words || !c || !count || count > AIPET_KWS_WORDS)
    return -EINVAL;
  if (!c->max_span || c->max_span > 1000 || !c->max_blank ||
      c->max_blank > c->max_span || !c->prefix_wait ||
      c->prefix_wait > c->max_blank || c->cooldown > 1000 ||
      !isfinite(c->start_floor) || c->start_floor <= 0 || c->start_floor > 1 ||
      !isfinite(c->strong_probability) || c->strong_probability <= 0 ||
      c->strong_probability > 1 || !c->min_strong || c->min_strong > AIPET_KWS_PHONES)
    return -EINVAL;
  for (i = 0; i < count; ++i) {
    if (!words[i].count || words[i].count > AIPET_KWS_PHONES ||
        words[i].count < c->min_strong || !isfinite(words[i].boost) ||
        words[i].boost < 0 || words[i].boost > 10 ||
        !isfinite(words[i].threshold) || words[i].threshold <= 0 || words[i].threshold > 1)
      return -EINVAL;
    for (j = 0; j < words[i].count; ++j)
      if (words[i].tokens[j] <= 0 || words[i].tokens[j] >= AIPET_KWS_VOCAB || words[i].tokens[j] == 2)
        return -EINVAL;
    for (j = 0; j < i; ++j)
      if (words[i].count == words[j].count &&
          !memcmp(words[i].tokens, words[j].tokens, words[i].count * sizeof(int32_t)))
        return -EINVAL;
  }
  memset(s, 0, sizeof(*s));
  s->config = *c; s->words = (uint32_t)count;
  memcpy(s->word, words, count * sizeof(*words)); s->magic = MAGIC;
  return 0;
}
int aipet_kws_reset(void *memory)
{
  struct search *s = memory;
  if (!valid(s)) return -EINVAL;
  s->frame = s->count = s->cooldown_until = 0;
  memset(s->paths, 0, sizeof(s->paths));
  return 0;
}
int aipet_kws_contexts(const void *memory, int32_t *pairs, size_t capacity)
{
  const struct search *s = memory;
  uint32_t i, j, count = 1;
  int32_t tmp[AIPET_KWS_CONTEXTS * 2] = {0};
  if (!valid(s) || !pairs) return -EINVAL;
  for (i = 0; i < s->count; ++i) {
    if (s->paths[i].used >= s->word[s->paths[i].word].count ||
        s->frame - s->paths[i].start > s->config.max_span ||
        s->frame - s->paths[i].last > s->config.max_blank) continue;
    for (j = 0; j < count; ++j)
      if (tmp[j*2] == s->paths[i].context[0] && tmp[j*2+1] == s->paths[i].context[1]) break;
    if (j == count) {
      tmp[count*2] = s->paths[i].context[0]; tmp[count*2+1] = s->paths[i].context[1]; ++count;
    }
  }
  if (capacity < count * 2) return -ENOSPC;
  memcpy(pairs, tmp, count * 2 * sizeof(int32_t));
  return (int)count;
}
static uint32_t row_for(const int32_t *pairs, uint32_t count, const struct path *p)
{
  uint32_t row;
  for (row = 0; row < count; ++row)
    if (pairs[row*2] == p->context[0] && pairs[row*2+1] == p->context[1]) break;
  return row;
}
static float probability(const float *v, int token)
{
  int i; double sum = 0; float peak = v[0];
  for (i = 1; i < AIPET_KWS_VOCAB; ++i) if (v[i] > peak) peak = v[i];
  for (i = 0; i < AIPET_KWS_VOCAB; ++i) sum += exp((double)v[i] - peak);
  return (float)(exp((double)v[token] - peak) / sum);
}
static struct path extend(struct path p, const struct aipet_kws_word *w,
                          const float *logits, uint32_t frame, float strong)
{
  int token = w->tokens[p.used];
  float prob = probability(logits, token);
  /* Log-odds against this context's blank. No forced continuation +5, no
   * replacement of quiet acoustic scores by manufactured probabilities. */
  p.score += logits[token] - logits[0] + w->boost;
  p.probability += prob; p.strong += prob >= strong;
  p.context[0] = p.context[1]; p.context[1] = token;
  ++p.used; p.last = frame;
  return p;
}
static int prefixes(const struct aipet_kws_word *a, const struct aipet_kws_word *b)
{
  return a->count < b->count && !memcmp(a->tokens, b->tokens, a->count * sizeof(int32_t));
}
static void add(struct path *out, uint32_t *count, struct path p)
{
  uint32_t i;
  /* At most one best alignment per word/position/context. Same context is
   * deterministic; retain age as part of selection through expiry, not a
   * forever-growing hypothesis history. */
  for (i = 0; i < *count; ++i)
    if (out[i].word == p.word && out[i].used == p.used) {
      if (p.score > out[i].score) out[i] = p;
      return;
    }
  if (*count < CANDIDATES) out[(*count)++] = p;
}
int aipet_kws_advance(void *memory, const float *logits, size_t rows,
                      struct aipet_kws_hit *hit)
{
  struct search *s = memory;
  struct path candidates[CANDIDATES];
  int32_t pairs[AIPET_KWS_CONTEXTS*2];
  uint32_t i, j, n = 0, frame;
  int contexts, chosen = -1;
  if (!valid(s) || !logits || !hit) return -EINVAL;
  contexts = aipet_kws_contexts(s, pairs, AIPET_KWS_CONTEXTS*2);
  if (contexts < 0 || rows != (size_t)contexts) return -EINVAL;
  for (i = 0; i < rows * AIPET_KWS_VOCAB; ++i)
    if (!isfinite(logits[i]) || fabsf(logits[i]) > 1000000) return -ERANGE;
  if (s->frame >= UINT32_MAX - 2000) return -EOVERFLOW;
  hit->word = -1; hit->frame = frame = s->frame;
  hit->mean_probability = hit->relative_score = 0;
  if (frame < s->cooldown_until) { ++s->frame; return 0; }
  for (i = 0; i < s->count; ++i) {
    struct path p = s->paths[i];
    const struct aipet_kws_word *w = &s->word[p.word];
    if (frame - p.start > s->config.max_span || frame - p.last > s->config.max_blank) continue;
    add(candidates, &n, p); /* blank retains the path, not just its greedy extension */
    if (p.used < w->count) {
      uint32_t row = row_for(pairs, (uint32_t)contexts, &p);
      const float *v = logits + row*AIPET_KWS_VOCAB;
      if (probability(v, w->tokens[p.used]) >= s->config.start_floor) {
        p = extend(p, w, v, frame, s->config.strong_probability);
        add(candidates, &n, p);
      }
    }
  }
  for (i = 0; i < s->words; ++i) {
    const struct aipet_kws_word *w = &s->word[i];
    if (probability(logits, w->tokens[0]) >= s->config.start_floor) {
      struct path p = {{0,0},i,0,frame,frame,0,0,0};
      add(candidates, &n, extend(p, w, logits, frame, s->config.strong_probability));
    }
  }
  for (i = 0; i < n; ++i) {
    struct path *p = &candidates[i];
    const struct aipet_kws_word *w = &s->word[p->word];
    int prefix = 0;
    if (p->used != w->count || p->strong < s->config.min_strong ||
        p->probability / p->used < w->threshold || p->score < 0) continue;
    for (j = 0; j < s->words; ++j) prefix |= prefixes(w, &s->word[j]);
    if (prefix && frame - p->last < s->config.prefix_wait) continue;
    if (chosen < 0 || p->used > candidates[chosen].used ||
        (p->used == candidates[chosen].used && p->score > candidates[chosen].score)) chosen = (int)i;
  }
  if (chosen >= 0) {
    struct path *p = &candidates[chosen];
    hit->word = (int32_t)p->word; hit->mean_probability = p->probability / p->used;
    hit->relative_score = p->score;
    s->count = 0; s->cooldown_until = frame + s->config.cooldown + 1;
  } else {
    /* Deterministic, bounded top paths; no heap, unbounded transcript or tree. */
    for (i = 0; i < n; ++i)
      for (j = i+1; j < n; ++j)
        if (candidates[j].score > candidates[i].score) {
          struct path tmp = candidates[i]; candidates[i] = candidates[j]; candidates[j] = tmp;
        }
    s->count = n < AIPET_KWS_PATHS ? n : AIPET_KWS_PATHS;
    memcpy(s->paths, candidates, s->count * sizeof(struct path));
  }
  ++s->frame;
  return 0;
}
