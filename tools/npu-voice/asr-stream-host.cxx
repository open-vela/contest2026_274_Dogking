/* SPDX-License-Identifier: Apache-2.0
 * Host-only real model oracle bridge. X=103x80 logical features ->24x512
 * rows; D/J same protocol as asr-decoder-host. No NBG/NPU impersonation. */
#include "voice_asr_stream.hxx"
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <vector>
static int exact(void *p, size_t n) {
  return fread(p, 1, n, stdin) == n ? 0 : -EIO;
}
static int send(const void *p, size_t n) {
  return fwrite(p, 1, n, stdout) == n ? 0 : -EIO;
}
static int begin(void *) { return 0; }
static void end(void *) {}
static int cancel(void *) { return 0; }
static int encode(void *, const float *f, float *out) {
  int32_t rc;
  if (send("X", 1) || send(f, 103 * 80 * 4) || fflush(stdout) || exact(&rc, 4))
    return -EIO;
  return rc ? rc : exact(out, 24 * 512 * 4);
}
static int decoder(void *, const int64_t y[2], float *out) {
  int32_t rc;
  if (send("D", 1) || send(y, 16) || fflush(stdout) || exact(&rc, 4))
    return -EIO;
  return rc ? rc : exact(out, 512 * 4);
}
static int joiner(void *, const float *e, const float *d, float *out) {
  int32_t rc;
  if (send("J", 1) || send(e, 2048) || send(d, 2048) || fflush(stdout) ||
      exact(&rc, 4))
    return -EIO;
  return rc ? rc : exact(out, 6254 * 4);
}
int main() {
  const aipet::VoiceAsrStreamOps ops = {
      begin, encode, end, {decoder, joiner, cancel}};
  aipet::VoiceAsrStream *s = nullptr;
  int32_t rc = aipet::voice_asr_stream_open(&ops, nullptr, &s);
  std::vector<unsigned char> pcm(6400);
  while (!rc) {
    uint32_t n = 0;
    rc = exact(&n, 4);
    if (rc || !n)
      break;
    if (n > pcm.size() || n % 2) {
      rc = -EINVAL;
      break;
    }
    if (exact(pcm.data(), n)) {
      rc = -EIO;
      break;
    }
    rc = aipet::voice_asr_stream_accept(s, pcm.data(), n);
    if (!rc) {
      aipet::VoiceAsrStreamStats st{};
      rc = aipet::voice_asr_stream_stats(s, &st);
      uint32_t stats[3] = {st.samples, st.chunks, st.frames};
      if (!rc && (send("K", 1) || send(stats, 12) || fflush(stdout)))
        rc = -EIO;
    }
  }
  if (!rc)
    rc = aipet::voice_asr_stream_finish(s);
  if (!rc) {
    std::vector<unsigned> t(1024), p(1024);
    size_t n = 0;
    rc = aipet::voice_asr_stream_result(s, t.data(), p.data(), 1024, &n);
    uint32_t used = n;
    if (!rc) {
      send("R", 1);
      send(&used, 4);
      for (unsigned i = 0; i < used; i++) {
        send(t.data() + i, 4);
        send(p.data() + i, 4);
      }
    }
  }
  if (rc) {
    send("E", 1);
    send(&rc, 4);
  }
  fflush(stdout);
  aipet::voice_asr_stream_close(s);
  return rc ? 2 : 0;
}
