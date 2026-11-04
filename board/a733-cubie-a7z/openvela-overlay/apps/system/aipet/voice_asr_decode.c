/* SPDX-License-Identifier: Apache-2.0 */
#include "voice_asr_decode.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct aipet_asr_decoder {
  struct aipet_asr_ops ops;
  void *cookie;
  int error, finished;
  uint32_t count, frames;
  int64_t context[2];
  float decoder[AIPET_ASR_DIM], logits[AIPET_ASR_VOCAB];
  uint32_t token[AIPET_ASR_MAX_TOKENS], position[AIPET_ASR_MAX_TOKENS];
};
static int fail(struct aipet_asr_decoder *s, int rc)
{
  s->error = rc < 0 ? rc : -EIO;
  return s->error;
}
static int poll_stop(struct aipet_asr_decoder *s)
{
  int rc = s->ops.cancelled(s->cookie);
  return rc ? fail(s, rc) : 0;
}
static int all_finite(const float *v, size_t n)
{
  size_t i;
  for (i = 0; i < n; ++i) if (!isfinite(v[i])) return 0;
  return 1;
}
static int decode(struct aipet_asr_decoder *s)
{
  size_t i;
  int rc;
  if (poll_stop(s)) return s->error;
  for (i = 0; i < AIPET_ASR_DIM; ++i) s->decoder[i] = NAN;
  rc = s->ops.decoder(s->cookie, s->context, s->decoder);
  if (rc) return fail(s, rc);
  if (!all_finite(s->decoder, AIPET_ASR_DIM)) return fail(s, -EILSEQ);
  return poll_stop(s);
}
int aipet_asr_decoder_open(const struct aipet_asr_ops *ops, void *cookie,
                          struct aipet_asr_decoder **out)
{
  struct aipet_asr_decoder *s;
  int rc;
  if (!out) return -EINVAL;
  *out = NULL;
  if (!ops) return -ENODEV;
  if (!ops->decoder || !ops->joiner || !ops->cancelled) return -EINVAL;
  s = calloc(1, sizeof(*s));
  if (!s) return -ENOMEM;
  s->ops = *ops; s->cookie = cookie; /* initial context = [blank,blank] */
  rc = decode(s);
  if (rc) { aipet_asr_decoder_close(s); return rc; }
  *out = s;
  return 0;
}
int aipet_asr_decoder_accept(struct aipet_asr_decoder *s, const float *encoder,
                            size_t frames)
{
  size_t frame, i;
  int rc;
  if (!s || (!encoder && frames)) return -EINVAL;
  if (s->error) return s->error;
  if (s->finished) return -EPIPE;
  if (frames > AIPET_ASR_MAX_FRAMES - s->frames) return fail(s, -E2BIG);
  if (!all_finite(encoder, frames * AIPET_ASR_DIM)) return fail(s, -EILSEQ);
  for (frame = 0; frame < frames; ++frame) {
    uint32_t best = 0;
    if (poll_stop(s)) return s->error;
    for (i = 0; i < AIPET_ASR_VOCAB; ++i) s->logits[i] = NAN;
    rc = s->ops.joiner(s->cookie, encoder + frame * AIPET_ASR_DIM,
                      s->decoder, s->logits);
    if (rc) return fail(s, rc);
    if (poll_stop(s)) return s->error;
    /* One complete finite+argmax scan. No token/context/decoder side effect
     * occurs before ALL 6254 logits passed. First index still wins ties. */
    if (!isfinite(s->logits[0])) return fail(s, -EILSEQ);
    for (i = 1; i < AIPET_ASR_VOCAB; ++i) {
      if (!isfinite(s->logits[i])) return fail(s, -EILSEQ);
      if (s->logits[i] > s->logits[best]) best = (uint32_t)i;
    }
    if (best != 0 && best != 2) {
      /* Never surface an unexpected special marker as an Agent command. */
      if (best == 1) return fail(s, -EILSEQ);
      if (s->count == AIPET_ASR_MAX_TOKENS) return fail(s, -ENOSPC);
      s->context[0] = s->context[1]; s->context[1] = best;
      rc = decode(s);
      if (rc) return rc;
      s->token[s->count] = best;
      s->position[s->count++] = s->frames;
    }
    ++s->frames;
  }
  return 0;
}
int aipet_asr_decoder_finish(struct aipet_asr_decoder *s)
{
  if (!s) return -EINVAL;
  if (s->error) return s->error;
  if (!s->frames) return fail(s, -ENODATA);
  if (poll_stop(s)) return s->error;
  s->finished = 1;
  return 0;
}
int aipet_asr_decoder_result(const struct aipet_asr_decoder *s, uint32_t *tokens,
                            uint32_t *positions, size_t capacity, size_t *used)
{
  if (!used) return -EINVAL;
  *used = 0;
  if (!s) return -EINVAL;
  if (s->error) return s->error;
  if (!s->finished) return -EAGAIN;
  if (capacity < s->count || (s->count && (!tokens || !positions))) return -ENOSPC;
  if (s->count) {
    memcpy(tokens, s->token, s->count * sizeof(*tokens));
    memcpy(positions, s->position, s->count * sizeof(*positions));
  }
  *used = s->count;
  return 0;
}
void aipet_asr_decoder_close(struct aipet_asr_decoder *s)
{
  if (s) { memset(s, 0, sizeof(*s)); free(s); }
}
