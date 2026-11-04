/* SPDX-License-Identifier: Apache-2.0 */
#include "../board/a733-cubie-a7z/openvela-overlay/apps/system/aipetasr/local_asr.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
struct mock { unsigned samples, frames, closes; int fail; };
static int open_engine(void *config, void **session) { *session = config; return 0; }
static int accept(void *session, const int16_t *data, size_t n)
{
  struct mock *m = session;
  size_t i;
  if (m->fail) return -EIO;
  for (i = 0; i < n; ++i) assert(data[i] == -32768);
  m->samples += n; ++m->frames; return 0;
}
static int result(void *session, char *text, size_t capacity, int final)
{
  (void)session;
  if (capacity < 2) return -ENOSPC;
  strcpy(text, final ? "F" : "P"); return 0; /* Mock, never a recognition claim. */
}
static void close_engine(void *session) { ++((struct mock *)session)->closes; }
int main(void)
{
  const struct local_asr_engine engine = {open_engine,accept,result,close_engine};
  unsigned char pcm[642];
  size_t split;
  struct local_asr_stream *stream;
  char text[8];
  memset(pcm,0,sizeof(pcm));
  for (split = 1; split < sizeof(pcm); split += 2) pcm[split] = 128;
  assert(local_asr_open(NULL,NULL,&stream) == -ENODEV && !stream);
  for (split = 0; split <= sizeof(pcm); ++split)
    {
      struct mock m = {0};
      struct local_asr_status status;
      assert(!local_asr_open(&engine,&m,&stream));
      assert(!local_asr_feed(stream,pcm,split));
      assert(!local_asr_feed(stream,pcm+split,sizeof(pcm)-split));
      assert(!local_asr_partial(stream,text,sizeof(text)) && !strcmp(text,"P"));
      assert(!local_asr_finish(stream,text,sizeof(text)) && !strcmp(text,"F"));
      assert(m.samples == 321 && m.frames == 2);
      assert(!local_asr_get_status(stream,&status) && status.samples_submitted == 321);
      assert(local_asr_feed(stream,pcm,2) == -EPIPE);
      local_asr_destroy(stream); assert(m.closes == 1);
    }
  {
    struct mock m = {0};
    assert(!local_asr_open(&engine,&m,&stream));
    assert(!local_asr_feed(stream,pcm,1));
    assert(local_asr_finish(stream,text,sizeof(text)) == -EINVAL);
    local_asr_destroy(stream);
    assert(!local_asr_open(&engine,&m,&stream));
    m.fail = 1;
    assert(local_asr_feed(stream,pcm,640) == -EIO);
    assert(local_asr_feed(stream,pcm,2) == -EPIPE);
    local_asr_destroy(stream);
    m.fail = 0;
    assert(!local_asr_open(&engine,&m,&stream));
    local_asr_abort(stream);
    assert(local_asr_finish(stream,text,sizeof(text)) == -EPIPE);
    local_asr_destroy(stream);
  }
  puts("local ASR stream tests passed (mock engine, no speech recognition)");
  return 0;
}
