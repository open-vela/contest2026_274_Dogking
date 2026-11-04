/* SPDX-License-Identifier: Apache-2.0 */
#include "audio_resample.hxx"
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace aipet;
struct Sink {
  std::vector<unsigned char> data;
  unsigned calls = 0;
  bool fail = false;
};
static int sink(void *p, const unsigned char *v, std::size_t n) {
  auto &s = *static_cast<Sink *>(p);
  ++s.calls;
  assert(n && n <= 960 && n % 2 == 0);
  if (s.fail)
    return -EPIPE;
  s.data.insert(s.data.end(), v, v + n);
  return 0;
}
static void le(unsigned char *v, uint32_t n, unsigned bytes) {
  for (unsigned i = 0; i < bytes; i++)
    v[i] = n >> (i * 8);
}
int main() {
  for (unsigned frames : {1u, 15u, 16u, 17u, 2205u, 51200u, 110250u, 220500u}) {
    std::vector<unsigned char> wav(44 + frames * 2), expected(960000);
    std::memcpy(wav.data(), "RIFF", 4);
    le(wav.data() + 4, 36 + frames * 2, 4);
    std::memcpy(wav.data() + 8, "WAVEfmt ", 8);
    le(wav.data() + 16, 16, 4);
    le(wav.data() + 20, 1, 2);
    le(wav.data() + 22, 1, 2);
    le(wav.data() + 24, 22050, 4);
    le(wav.data() + 28, 44100, 4);
    le(wav.data() + 32, 2, 2);
    le(wav.data() + 34, 16, 2);
    std::memcpy(wav.data() + 36, "data", 4);
    le(wav.data() + 40, frames * 2, 4);
    uint32_t seed = 93;
    for (unsigned i = 0; i < frames; i++) {
      seed = seed * 1664525 + 1013904223;
      le(wav.data() + 44 + i * 2, seed >> 16, 2);
    }
    std::size_t used = 0;
    assert(!matcha_wav_to_48k(wav.data(), wav.size(), expected.data(),
                              expected.size(), &used));
    expected.resize(used);
    for (unsigned step : {1u, 147u, 512u, 51200u}) {
      Sink out;
      MatchaPcmStream *s = nullptr;
      assert(!matcha_pcm_stream_open(sink, &out, &s));
      bool early = false;
      for (unsigned i = 0; i < frames;) {
        unsigned n = frames - i;
        if (n > step)
          n = step;
        assert(!matcha_pcm_stream_accept(s, wav.data() + 44 + i * 2, n * 2));
        i += n;
        if (i < frames && out.calls)
          early = true;
      }
      assert(!matcha_pcm_stream_finish(s));
      assert(out.data == expected);
      assert(matcha_pcm_stream_finish(s) == -EALREADY);
      if (frames >= 2205 && step < frames)
        assert(early);
      matcha_pcm_stream_close(s);
    }
    std::printf("stream-resample: frames=%u exact whole-WAV equivalence; chunk "
                "sizes 1/147/512/51200 PASS\n",
                frames);
  }
  Sink out;
  out.fail = true;
  MatchaPcmStream *s = nullptr;
  assert(!matcha_pcm_stream_open(sink, &out, &s));
  std::vector<unsigned char> pcm(4410);
  assert(matcha_pcm_stream_accept(s, pcm.data(), pcm.size()) == -EPIPE);
  unsigned calls = out.calls;
  assert(matcha_pcm_stream_finish(s) == -EPIPE && out.calls == calls);
  matcha_pcm_stream_close(s);
  puts("PASS: sticky sink failure, bounded streaming, no per-chunk phase "
       "restart");
}
