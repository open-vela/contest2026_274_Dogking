/* SPDX-License-Identifier: Apache-2.0 */
#include "sherpa_engine.h"
#include "sherpa-ncnn/c-api/c-api.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct sherpa_session
{
  SherpaNcnnRecognizer *recognizer;
  SherpaNcnnStream *stream;
  int finished;
};

static void close_session(void *context)
{
  struct sherpa_session *session = context;
  if (!session) return;
  if (session->stream) DestroyStream(session->stream);
  if (session->recognizer) DestroyRecognizer(session->recognizer);
  free(session);
}

static int check_file(const char *path)
{
  FILE *file;
  long size;
  if (!path || !*path) return -EINVAL;
  file = fopen(path,"rb");
  if (!file) return -errno;
  if (fseek(file,0,SEEK_END)) { fclose(file); return -EIO; }
  size = ftell(file);
  fclose(file);
  return size > 0 ? 0 : -EINVAL;
}

static int open_session(void *context, void **out)
{
  const struct local_asr_sherpa_config *input = context;
  SherpaNcnnRecognizerConfig config;
  struct sherpa_session *session;
  const char *paths[7];
  unsigned i;
  if (!out || !input) return -EINVAL;
  *out = NULL;
  if (!input->threads || input->threads > 8) return -EINVAL;
  paths[0] = input->encoder_param; paths[1] = input->encoder_bin;
  paths[2] = input->decoder_param; paths[3] = input->decoder_bin;
  paths[4] = input->joiner_param; paths[5] = input->joiner_bin;
  paths[6] = input->tokens;
  for (i = 0; i < 7; ++i)
    { int result = check_file(paths[i]); if (result) return result; }
  memset(&config,0,sizeof(config));
  config.feat_config.sampling_rate = 16000;
  config.feat_config.feature_dim = 80;
  config.model_config.encoder_param = paths[0];
  config.model_config.encoder_bin = paths[1];
  config.model_config.decoder_param = paths[2];
  config.model_config.decoder_bin = paths[3];
  config.model_config.joiner_param = paths[4];
  config.model_config.joiner_bin = paths[5];
  config.model_config.tokens = paths[6];
  config.model_config.num_threads = (int)input->threads;
  config.model_config.use_vulkan_compute = 0;
  config.decoder_config.decoding_method = "greedy_search";
  config.decoder_config.num_active_paths = 4;
  /* Explicit finish controls this stage; endpointing is a later service API. */
  config.enable_endpoint = 0;
  session = calloc(1,sizeof(*session));
  if (!session) return -ENOMEM;
  session->recognizer = CreateRecognizer(&config);
  if (session->recognizer) session->stream = CreateStream(session->recognizer);
  if (!session->stream) { close_session(session); return -EIO; }
  *out = session;
  return 0;
}

static int drain(struct sherpa_session *session)
{
  unsigned steps = 0;
  while (IsReady(session->recognizer,session->stream))
    {
      /* Progress guard, not a wall-clock timeout. */
      if (++steps > 4096) return -ELOOP;
      Decode(session->recognizer,session->stream);
    }
  return 0;
}

static int accept(void *context, const int16_t *pcm, size_t count)
{
  struct sherpa_session *session = context;
  float samples[LOCAL_ASR_FRAME_SAMPLES];
  size_t i;
  if (!session || session->finished || !pcm || !count || count > LOCAL_ASR_FRAME_SAMPLES)
    return -EINVAL;
  for (i = 0; i < count; ++i) samples[i] = pcm[i] / 32768.0f;
  AcceptWaveform(session->stream,16000,samples,(int)count);
  return drain(session);
}

static int result(void *context, char *text, size_t capacity, int final)
{
  struct sherpa_session *session = context;
  SherpaNcnnResult *decoded;
  int error;
  size_t length;
  if (!session || session->finished || !text || !capacity) return -EINVAL;
  text[0] = 0;
  if (final)
    {
      /* Same 0.3s tail as upstream file example, added in bounded chunks. */
      const float silence[LOCAL_ASR_FRAME_SAMPLES] = {0};
      unsigned i;
      for (i = 0; i < 15; ++i)
        {
          AcceptWaveform(session->stream,16000,silence,LOCAL_ASR_FRAME_SAMPLES);
          error = drain(session); if (error) return error;
        }
      InputFinished(session->stream);
      session->finished = 1;
    }
  error = drain(session); if (error) return error;
  decoded = GetResult(session->recognizer,session->stream);
  if (!decoded) return -EIO;
  if (!decoded->text) { DestroyResult(decoded); return -EIO; }
  length = strlen(decoded->text);
  if (length >= capacity) { DestroyResult(decoded); return -ENOSPC; }
  memcpy(text,decoded->text,length + 1);
  DestroyResult(decoded);
  return 0;
}

const struct local_asr_engine local_asr_sherpa_engine =
  {open_session,accept,result,close_session};
