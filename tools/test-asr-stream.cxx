/* SPDX-License-Identifier: Apache-2.0 */
#include "voice_asr_stream.hxx"
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <vector>
using namespace aipet;
struct State {
  unsigned starts = 0, ends = 0, encodes = 0;
  int error = 0;
  bool missing = false;
};
static int begin(void *p) {
  ++static_cast<State *>(p)->starts;
  return 0;
}
static void end(void *p) { ++static_cast<State *>(p)->ends; }
static int encode(void *p, const float *, float *out) {
  auto &s = *static_cast<State *>(p);
  ++s.encodes;
  if (s.error)
    return s.error;
  if (!s.missing)
    std::fill(out, out + 24 * 512, 0);
  return 0;
}
static int decoder(void *, const int64_t *, float *out) {
  std::fill(out, out + 512, 0);
  return 0;
}
static int joiner(void *, const float *, const float *, float *out) {
  std::fill(out, out + 6254, -1);
  out[0] = 1;
  return 0;
}
static int cancel(void *) { return 0; }
int main() {
  const VoiceAsrStreamOps ops = {begin, encode, end, {decoder, joiner, cancel}};
  for (unsigned samples : {160u, 46080u, 48000u, 128000u}) {
    State state;
    VoiceAsrStream *s = nullptr;
    assert(!voice_asr_stream_open(&ops, &state, &s));
    std::vector<unsigned char> pcm(samples * 2);
    size_t used = 99;
    unsigned tokens[1024], positions[1024];
    assert(voice_asr_stream_result(s, tokens, positions, 1024, &used) ==
               -EAGAIN &&
           !used);
    for (unsigned at = 0; at < samples;) {
      unsigned n = std::min(1600u, samples - at);
      assert(!voice_asr_stream_accept(s, pcm.data() + at * 2, n * 2));
      at += n;
    }
    if (samples >= 48000)
      assert(state.encodes > 0);
    assert(!voice_asr_stream_finish(s));
    unsigned expected = (((samples + 80) / 160) + 95) / 96;
    assert(state.encodes == expected);
    VoiceAsrStreamStats st{};
    assert(!voice_asr_stream_stats(s, &st));
    assert(st.samples == samples && st.chunks == expected &&
           st.frames == 24 * expected && st.finished);
    assert(!voice_asr_stream_result(s, tokens, positions, 1024, &used) &&
           used == 0);
    assert(voice_asr_stream_finish(s) == -EALREADY);
    voice_asr_stream_close(s);
    assert(state.starts == 1 && state.ends == 1);
  }
  for (bool missing : {false, true}) {
    State state;
    state.error = missing ? 0 : -EIO;
    state.missing = missing;
    VoiceAsrStream *s = nullptr;
    assert(!voice_asr_stream_open(&ops, &state, &s));
    std::vector<unsigned char> pcm(35200);
    int expected = missing ? -ERANGE : -EIO;
    assert(voice_asr_stream_accept(s, pcm.data(), pcm.size()) == expected);
    assert(voice_asr_stream_finish(s) == expected);
    voice_asr_stream_close(s);
    assert(state.ends == 1);
  }
  puts("PASS: online ASR early scheduling, exact chunk/tail boundaries, no "
       "partial publication, sticky failures and state cleanup (mock engines)");
}
