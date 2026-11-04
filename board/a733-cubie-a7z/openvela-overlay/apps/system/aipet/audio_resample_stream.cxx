/* SPDX-License-Identifier: Apache-2.0 */
#include "audio_resample.hxx"
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <new>
namespace aipet {
struct MatchaPcmStream {
  double taps[320][32];
  int16_t ring[64]{}, first = 0, last = 0;
  unsigned input = 0, output = 0, used = 0;
  unsigned max_samples=220500;
  unsigned char pcm[960];
  MatchaPcmSink sink;
  void *cookie;
  int error = 0;
  bool done = false;
};
static int send(MatchaPcmStream *s) {
  if (!s->used)
    return 0;
  int rc = s->sink(s->cookie, s->pcm, s->used);
  s->used = 0;
  if (rc)
    s->error = rc < 0 ? rc : -EIO;
  return s->error;
}
static int produce(MatchaPcmStream *s, bool finish) {
  unsigned wanted = (s->input * 320u + 146u) / 147u;
  while (s->output < wanted) {
    unsigned base = s->output * 147u / 320u, phase = s->output * 147u % 320u;
    if (!finish && base + 16 >= s->input)
      break;
    double value = 0;
    for (int k = 0; k < 32; k++) {
      int at = static_cast<int>(base) + k - 15;
      int sample = at < 0                                  ? s->first
                   : static_cast<unsigned>(at) >= s->input ? s->last
                                                           : s->ring[at % 64];
      value += sample * s->taps[phase][k];
    }
    long sample = std::lround(value);
    if (sample > 32767)
      sample = 32767;
    if (sample < -32768)
      sample = -32768;
    uint16_t raw = static_cast<uint16_t>(sample);
    s->pcm[s->used++] = raw & 255;
    s->pcm[s->used++] = raw >> 8;
    ++s->output;
    if (s->used == sizeof(s->pcm) && send(s))
      return s->error;
  }
  return 0;
}
int matcha_pcm_stream_open_limit(MatchaPcmSink sink, void *cookie,
                           MatchaPcmStream **out,unsigned max_samples) {
  if (!out)
    return -EINVAL;
  *out = nullptr;
  if (!sink || !max_samples || max_samples>1323000)
    return -EINVAL;
  auto *s = new (std::nothrow) MatchaPcmStream;
  if (!s)
    return -ENOMEM;
  s->sink = sink;
  s->cookie = cookie;
  s->max_samples=max_samples;
  constexpr double pi = 3.14159265358979323846;
  for (unsigned p = 0; p < 320; p++) {
    double sum = 0;
    for (int k = 0; k < 32; k++) {
      double x = double(k - 15) - double(p) / 320;
      double sinc =
          std::fabs(x) < 1e-12 ? 0.9 : std::sin(0.9 * pi * x) / (pi * x);
      double window = std::fabs(x) < 16 ? 0.5 + 0.5 * std::cos(pi * x / 16) : 0;
      s->taps[p][k] = sinc * window;
      sum += s->taps[p][k];
    }
    for (double &tap : s->taps[p])
      tap /= sum;
  }
  *out = s;
  return 0;
}
int matcha_pcm_stream_open(MatchaPcmSink sink,void *cookie,MatchaPcmStream **out)
{return matcha_pcm_stream_open_limit(sink,cookie,out,220500);}
int matcha_pcm_stream_accept(MatchaPcmStream *s, const unsigned char *pcm,
                             std::size_t bytes) {
  if (!s)
    return -EINVAL;
  if (s->error)
    return s->error;
  if (s->done)
    return -EALREADY;
  if (!pcm || !bytes || bytes % 2 || bytes / 2 > s->max_samples - s->input)
    return s->error = -EINVAL;
  for (std::size_t i = 0; i < bytes; i += 2) {
    unsigned raw = pcm[i] | (unsigned(pcm[i + 1]) << 8);
    s->last = raw >= 32768 ? int(raw) - 65536 : raw;
    if (!s->input)
      s->first = s->last;
    s->ring[s->input % 64] = s->last;
    ++s->input;
    if (produce(s, false))
      return s->error;
  }
  return 0;
}
int matcha_pcm_stream_finish(MatchaPcmStream *s) {
  if (!s)
    return -EINVAL;
  if (s->error)
    return s->error;
  if (s->done)
    return -EALREADY;
  if (!s->input)
    return s->error = -ENODATA;
  int rc = produce(s, true);
  if (!rc)
    rc = send(s);
  if (!rc)
    s->done = true;
  return rc;
}
void matcha_pcm_stream_close(MatchaPcmStream *s) {
  if (s) {
    volatile unsigned char *p =
        reinterpret_cast<volatile unsigned char *>(s->ring);
    for (unsigned i = 0; i < sizeof(s->ring); i++)
      p[i] = 0;
    p = s->pcm;
    for (unsigned i = 0; i < sizeof(s->pcm); i++)
      p[i] = 0;
  }
  delete s;
}
} // namespace aipet
