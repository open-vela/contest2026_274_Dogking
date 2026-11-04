/* SPDX-License-Identifier: Apache-2.0 */
#include "sherpa_engine.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

/* PCM file validation is independent of USB audio. No WAV-header assumptions. */
#ifdef LOCAL_ASR_HOST
int main(int argc, char **argv)
#else
int asr_main(int argc, char **argv)
#endif
{
  struct local_asr_sherpa_config config;
  struct local_asr_stream *stream = NULL;
  struct local_asr_status status;
  unsigned char pcm[640];
  char text[4096];
  FILE *input;
  size_t received;
  int result;
  unsigned threads = 2;
  if (argc != 9 && argc != 10)
    {
      fprintf(stderr,"Usage: asr TOKENS ENC_PARAM ENC_BIN DEC_PARAM DEC_BIN "
          "JOIN_PARAM JOIN_BIN PCM_S16LE_16K_MONO [THREADS_1_8]\n");
      return 1;
    }
  if (argc == 10)
    {
      char *end;
      unsigned long parsed;
      errno = 0;
      parsed = strtoul(argv[9],&end,10);
      if (errno || end == argv[9] || *end || !parsed || parsed > 8) return 1;
      threads = (unsigned)parsed;
    }
  config.tokens = argv[1];
  config.encoder_param = argv[2]; config.encoder_bin = argv[3];
  config.decoder_param = argv[4]; config.decoder_bin = argv[5];
  config.joiner_param = argv[6]; config.joiner_bin = argv[7];
  config.threads = threads;
  input = fopen(argv[8],"rb");
  if (!input) { fprintf(stderr,"asr: PCM open error=%d\n",errno); return 1; }
  result = local_asr_open(&local_asr_sherpa_engine,&config,&stream);
  while (!result && (received = fread(pcm,1,sizeof(pcm),input)) != 0)
    result = local_asr_feed(stream,pcm,received);
  if (!result && ferror(input)) result = -EIO;
  fclose(input);
  if (!result) result = local_asr_finish(stream,text,sizeof(text));
  if (!result)
    {
      puts(text);
      if (!local_asr_get_status(stream,&status))
        fprintf(stderr,"asr: samples=%llu frames=%u final=1\n",
            (unsigned long long)status.samples_submitted,status.frames);
    }
  else fprintf(stderr,"asr: failed error=%d\n",result);
  local_asr_destroy(stream);
  return result ? 1 : 0;
}
