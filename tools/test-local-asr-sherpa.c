/* SPDX-License-Identifier: Apache-2.0 */
#include "../board/a733-cubie-a7z/openvela-overlay/apps/system/aipetasr/sherpa_engine.h"
#include "sherpa-ncnn/c-api/c-api.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
/* This substitutes neural computation only; signatures use upstream header. */
struct SherpaNcnnRecognizer { int marker; };
struct SherpaNcnnStream { unsigned samples, ready, finished; };
static unsigned released, results_released;
SherpaNcnnRecognizer *CreateRecognizer(const SherpaNcnnRecognizerConfig *config)
{
  assert(config->feat_config.sampling_rate == 16000);
  assert(config->feat_config.feature_dim == 80);
  assert(config->model_config.num_threads == 2 && !config->model_config.use_vulkan_compute);
  return calloc(1,sizeof(SherpaNcnnRecognizer));
}
void DestroyRecognizer(SherpaNcnnRecognizer *r) { ++released; free(r); }
SherpaNcnnStream *CreateStream(SherpaNcnnRecognizer *r)
{ assert(r); return calloc(1,sizeof(SherpaNcnnStream)); }
void DestroyStream(SherpaNcnnStream *s) { ++released; free(s); }
void AcceptWaveform(SherpaNcnnStream *s,float rate,const float *samples,int32_t count)
{
  int i;
  assert(rate == 16000 && !s->finished);
  for (i = 0; i < count; ++i) assert(samples[i] == -1 || samples[i] == 0);
  s->samples += count; ++s->ready;
}
int32_t IsReady(SherpaNcnnRecognizer *r,SherpaNcnnStream *s)
{ assert(r); return s->ready != 0; }
void Decode(SherpaNcnnRecognizer *r,SherpaNcnnStream *s)
{ assert(r && s->ready); --s->ready; }
void InputFinished(SherpaNcnnStream *s) { assert(!s->finished); s->finished = 1; }
SherpaNcnnResult *GetResult(SherpaNcnnRecognizer *r,SherpaNcnnStream *s)
{
  SherpaNcnnResult *result = calloc(1,sizeof(*result));
  assert(r);
  if (s->finished) assert(s->samples == 321 + 4800);
  result->text = s->finished ? "final" : "partial";
  return result;
}
void DestroyResult(const SherpaNcnnResult *r) { ++results_released; free((void *)r); }
int main(void)
{
  struct local_asr_sherpa_config config =
    {__FILE__,__FILE__,__FILE__,__FILE__,__FILE__,__FILE__,__FILE__,2};
  struct local_asr_stream *s;
  unsigned char pcm[642];
  unsigned i;
  char text[32];
  memset(pcm,0,sizeof(pcm));
  for (i = 1; i < sizeof(pcm); i += 2) pcm[i] = 128;
  assert(!local_asr_open(&local_asr_sherpa_engine,&config,&s));
  assert(!local_asr_feed(s,pcm,sizeof(pcm)));
  assert(!local_asr_partial(s,text,sizeof(text)) && !strcmp(text,"partial"));
  assert(!local_asr_finish(s,text,sizeof(text)) && !strcmp(text,"final"));
  local_asr_destroy(s);
  assert(released == 2 && results_released == 2);
  config.tokens = "/missing/model/tokens.txt";
  assert(local_asr_open(&local_asr_sherpa_engine,&config,&s) == -ENOENT && !s);
  puts("Sherpa C API adapter tests passed (mock neural computation)");
}
