/* SPDX-License-Identifier: Apache-2.0 */
#include "local_asr.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

struct local_asr_stream
{
  struct local_asr_engine engine;
  void *session;
  struct local_asr_status status;
  int16_t frame[LOCAL_ASR_FRAME_SAMPLES];
  size_t used;
  unsigned char low;
  int has_low;
};

static int failed(struct local_asr_stream *s, int error)
{
  s->status.state = LOCAL_ASR_FAILED;
  s->status.last_error = error < 0 ? error : -EIO;
  return s->status.last_error;
}

int local_asr_open(const struct local_asr_engine *ops, void *config,
                   struct local_asr_stream **out)
{
  struct local_asr_stream *s;
  int result;
  if (!out) return -EINVAL;
  *out = NULL;
  if (!ops) return -ENODEV; /* No installed inference engine. */
  if (!ops->open || !ops->accept || !ops->result || !ops->close) return -EINVAL;
  s = calloc(1, sizeof(*s));
  if (!s) return -ENOMEM;
  s->engine = *ops;
  result = ops->open(config, &s->session);
  if (result || !s->session)
    {
      if (s->session) ops->close(s->session);
      free(s);
      return result < 0 ? result : -EIO;
    }
  s->status.state = LOCAL_ASR_ACTIVE;
  *out = s;
  return 0;
}

static int submit(struct local_asr_stream *s)
{
  int result;
  if (!s->used) return 0;
  result = s->engine.accept(s->session, s->frame, s->used);
  if (result) return failed(s, result);
  s->status.samples_submitted += s->used;
  ++s->status.frames;
  s->used = 0;
  return 0;
}

int local_asr_feed(struct local_asr_stream *s, const void *data, size_t size)
{
  const unsigned char *bytes = data;
  size_t i;
  if (!s || (!data && size)) return -EINVAL;
  if (s->status.state != LOCAL_ASR_ACTIVE) return -EPIPE;
  for (i = 0; i < size; ++i)
    {
      ++s->status.bytes_received;
      if (!s->has_low) { s->low = bytes[i]; s->has_low = 1; }
      else
        {
          unsigned value = s->low | ((unsigned)bytes[i] << 8);
          s->frame[s->used++] = (int16_t)(value < 32768 ? (int)value : (int)value - 65536);
          s->has_low = 0;
          if (s->used == LOCAL_ASR_FRAME_SAMPLES)
            { int result = submit(s); if (result) return result; }
        }
    }
  return 0;
}

static int read_result(struct local_asr_stream *s, char *text, size_t cap, int final)
{
  int result;
  if (!text || !cap) return -EINVAL;
  memset(text, 0, cap);
  result = s->engine.result(s->session, text, cap, final);
  if (result || !memchr(text, 0, cap))
    { text[0] = 0; return failed(s, result ? result : -ENOSPC); }
  return 0;
}

int local_asr_partial(struct local_asr_stream *s, char *text, size_t cap)
{
  if (!s) return -EINVAL;
  if (s->status.state != LOCAL_ASR_ACTIVE) return -EPIPE;
  return read_result(s, text, cap, 0);
}

int local_asr_finish(struct local_asr_stream *s, char *text, size_t cap)
{
  int result;
  if (!s || !text || !cap) return -EINVAL;
  text[0] = 0;
  if (s->status.state != LOCAL_ASR_ACTIVE) return -EPIPE;
  if (s->has_low) return failed(s, -EINVAL);
  result = submit(s);
  if (!result) result = read_result(s, text, cap, 1);
  if (!result) s->status.state = LOCAL_ASR_FINISHED;
  return result;
}

void local_asr_abort(struct local_asr_stream *s)
{
  if (s) { s->used = 0; s->has_low = 0; s->status.state = LOCAL_ASR_ABORTED; }
}

void local_asr_destroy(struct local_asr_stream *s)
{
  if (!s) return;
  s->engine.close(s->session);
  memset(s, 0, sizeof(*s));
  free(s);
}

int local_asr_get_status(const struct local_asr_stream *s, struct local_asr_status *out)
{
  if (!s || !out) return -EINVAL;
  *out = s->status;
  return 0;
}
