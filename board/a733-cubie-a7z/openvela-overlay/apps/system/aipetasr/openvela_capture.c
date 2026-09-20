/* SPDX-License-Identifier: Apache-2.0 */
#include "local_asr.h"
#include <media_recorder.h>
#include <errno.h>

/* Owner-thread, single utterance bounded capture. Service must stop/release
 * capture before speech playback. read_data may block: USB audio driver's
 * bounded reads and service cancellation remain separate integration work. */
int local_asr_capture_media(struct local_asr_stream *stream, unsigned milliseconds)
{
  void *recorder;
  unsigned char pcm[640];
  size_t remaining;
  int result;
  if (!stream || !milliseconds || milliseconds > 30000) return -EINVAL;
  recorder = media_recorder_open(MEDIA_SOURCE_MIC);
  if (!recorder) return -ENODEV;
  result = media_recorder_prepare(recorder, NULL,
      "format=s16le:sample_rate=16000:ch_layout=mono");
  if (!result) result = media_recorder_start(recorder);
  remaining = (size_t)milliseconds * 32;
  while (!result && remaining)
    {
      size_t wanted = remaining < sizeof(pcm) ? remaining : sizeof(pcm);
      int received = media_recorder_read_data(recorder, pcm, wanted);
      if (received <= 0 || (size_t)received > wanted)
        { result = received < 0 ? received : -EIO; break; }
      result = local_asr_feed(stream, pcm, (size_t)received);
      remaining -= (size_t)received;
    }
  {
    int cleanup = media_recorder_stop(recorder);
    if (!result && cleanup) result = cleanup;
    cleanup = media_recorder_close(recorder);
    if (!result && cleanup) result = cleanup;
  }
  return result;
}
