/* SPDX-License-Identifier: Apache-2.0 */
#include "voice_asr_stream.hxx"
#include "voice_fbank.hxx"
#include <cerrno>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
namespace aipet {
struct VoiceAsrStream {
  VoiceAsrStreamOps ops{};
  void *cookie = nullptr;
  VoiceFbank *fbank = nullptr;
  aipet_asr_decoder *decoder = nullptr;
  float features[103 * 80], rows[24 * 512];
  unsigned samples = 0, next = 0, chunks = 0, real_frames = 0;
  unsigned padded_frames=0;
  int error = 0;
  bool begun = false, finished = false, flushing = false;
};
static int consume(VoiceAsrStream *s) {
  int available = voice_fbank_frames(s->fbank);
  if (available < 0)
    return s->error = available;
  while (static_cast<unsigned>(available) >= s->next + 103 &&
         (!s->flushing || s->next < s->real_frames)) {
    int rc = s->ops.greedy.cancelled(s->cookie);
    if (rc)
      return s->error = rc < 0 ? rc : -ECANCELED;
    rc = voice_fbank_copy(s->fbank, s->next, 103, s->features, 103 * 80);
    if (rc)
      return s->error = rc;
    for (float &v : s->rows)
      v = std::numeric_limits<float>::quiet_NaN();
    rc = s->ops.encode(s->cookie, s->features, s->rows);
    if (rc)
      return s->error = rc < 0 ? rc : -EIO;
    for (float v : s->rows)
      if (!std::isfinite(v))
        return s->error = -ERANGE;
    rc = aipet_asr_decoder_accept(s->decoder, s->rows, 24);
    if (rc)
      return s->error = rc;
    s->next += 96;
    ++s->chunks;
  }
  return 0;
}
int voice_asr_stream_open(const VoiceAsrStreamOps *ops, void *cookie,
                          VoiceAsrStream **out) {
  if (!out)
    return -EINVAL;
  *out = nullptr;
  if (!ops || !ops->begin || !ops->encode || !ops->end ||
      !ops->greedy.decoder || !ops->greedy.joiner || !ops->greedy.cancelled)
    return -EINVAL;
  auto *s = new (std::nothrow) VoiceAsrStream;
  if (!s)
    return -ENOMEM;
  s->ops = *ops;
  s->cookie = cookie;
  int rc = voice_fbank_create(&s->fbank);
  if (!rc) {
    rc = s->ops.begin(cookie);
    s->begun = rc == 0;
  }
  if (!rc)
    rc = aipet_asr_decoder_open(&ops->greedy, cookie, &s->decoder);
  if (rc) {
    voice_asr_stream_close(s);
    return rc < 0 ? rc : -EIO;
  }
  *out = s;
  return 0;
}
int voice_asr_stream_accept(VoiceAsrStream *s, const unsigned char *pcm,
                            std::size_t bytes) {
  if (!s)
    return -EINVAL;
  if (s->error)
    return s->error;
  if (s->finished)
    return -EALREADY;
  if (!pcm || !bytes || bytes % 2 || bytes / 2 > 128000 - s->samples)
    return s->error = -EINVAL;
  int rc = voice_fbank_accept(s->fbank, pcm, bytes);
  if (rc)
    return s->error = rc;
  s->samples += bytes / 2;
  return consume(s);
}
int voice_asr_stream_finish(VoiceAsrStream *s) {
  if (!s)
    return -EINVAL;
  if (s->error)
    return s->error;
  if (s->finished)
    return -EALREADY;
  if (!s->samples)
    return s->error = -ENODATA;
  s->flushing = true;
  s->real_frames = (s->samples + 80) / 160;
  // Same normalized *waveform* zero tail as the locked reference. Never
  // pad with zero-valued log-mel (which is not silence in this frontend).
  const unsigned char zeros[320]{};
  for (unsigned i = 0; i < 103 && s->next < s->real_frames; i++) {
    ++s->padded_frames;
    int rc = voice_fbank_accept(s->fbank, zeros, sizeof(zeros));
    if (!rc)
      rc = consume(s);
    if (rc)
      return s->error = rc;
  }
  // Once all real-frame windows have been encoded, more waveform zeros can
  // only create unused feature frames. Still finalize the frontend and decoder.
  int rc = voice_fbank_finish(s->fbank);
  if (!rc)
    rc = consume(s);
  if (!rc)
    rc = aipet_asr_decoder_finish(s->decoder);
  if (rc)
    return s->error = rc;
  s->finished = true;
  return 0;
}
int voice_asr_stream_result(const VoiceAsrStream *s, unsigned *tokens,
                            unsigned *positions, std::size_t cap,
                            std::size_t *used) {
  if (used)
    *used = 0;
  if (!s)
    return -EINVAL;
  if (s->error)
    return s->error;
  if (!s->finished)
    return -EAGAIN;
  return aipet_asr_decoder_result(s->decoder, tokens, positions, cap, used);
}
int voice_asr_stream_stats(const VoiceAsrStream *s, VoiceAsrStreamStats *out) {
  if (!s || !out)
    return -EINVAL;
  *out = {s->samples, s->chunks, s->chunks * 24, s->finished,s->padded_frames};
  return s->error;
}
void voice_asr_stream_close(VoiceAsrStream *s) {
  if (!s)
    return;
  aipet_asr_decoder_close(s->decoder);
  if (s->begun)
    s->ops.end(s->cookie);
  voice_fbank_destroy(s->fbank);
  delete s;
}
} // namespace aipet
