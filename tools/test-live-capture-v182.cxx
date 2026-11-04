/* SPDX-License-Identifier: Apache-2.0 */
#include "audio_capture_live.hxx"
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <system/a733_pcm.h>
#include <unistd.h>
#include <vector>
#include <pthread.h>
static bool inject_join_failure=false;
static pthread_t retained_worker{};
extern "C" int __real_pthread_join(pthread_t,void **);
extern "C" int __wrap_pthread_join(pthread_t worker,void **result) {
  if(inject_join_failure){inject_join_failure=false;retained_worker=worker;return EINVAL;}
  return __real_pthread_join(worker,result);
}
extern "C" void __lsan_disable();
extern "C" void __lsan_enable();
using namespace aipet;
namespace aipet {
int audio_decimate(const unsigned char *, std::size_t, unsigned char *,
                   std::size_t, std::size_t *);
}
static unsigned mode = 0, calls = 0, cancelled = 0;
static bool slow_once=false;
static std::vector<unsigned char> raw48, actual16;
static int sink16(void *, const unsigned char *pcm, std::size_t bytes) {
  if(slow_once){slow_once=false;usleep(250000);} // endpoint must not wait for this consumer
  if (mode == 2)
    return -EIO;
  actual16.insert(actual16.end(), pcm, pcm + bytes);
  return 0;
}
static int sink48(void *, const unsigned char *pcm, std::size_t bytes) {
  raw48.insert(raw48.end(), pcm, pcm + bytes);
  return 0;
}
static int cancel(void *) { return cancelled ? -ECANCELED : 0; }
extern "C" int a733_pcm_capture_stream(unsigned ms, unsigned gain,
                                       a733_pcm_capture_sink sink, void *cookie,
                                       uint32_t *frames, unsigned *ended) {
  ++calls;
  assert(gain == 5);
  *frames = *ended = 0;
  unsigned total = ms * 48u, at = 0;
  unsigned char pcm[960]{};
  while (at < total) {
    unsigned n = std::min(480u, total - at);
    for (unsigned i = 0; i < n; i++) {
      int16_t sample = mode == 3 ? 0 : mode == 7 && at + i > 24000 ? 0 : 5000;
      pcm[i * 2] = uint16_t(sample) & 255;
      pcm[i * 2 + 1] = uint16_t(sample) >> 8;
    }
    if (mode == 1 && at >= 4800)
      return -EOVERFLOW;
    int rc = sink(cookie, pcm, mode == 6 ? 961 : n * 2);
    if (rc < 0)
      return rc;
    at += n;
    usleep(1000);
    if (rc == 1) {
      *frames = at;
      *ended = 1;
      return 0;
    }
  }
  *frames = mode == 4 ? total - 3 : total;
  return 0;
}
struct Samples {
  std::vector<unsigned char> bytes;
  bool fail = false;
};
static int collect(void *p, const unsigned char *pcm, std::size_t bytes) {
  auto *s = static_cast<Samples *>(p);
  if (s->fail)
    return -EPIPE;
  s->bytes.insert(s->bytes.end(), pcm, pcm + bytes);
  return 0;
}
int main() {
  for (unsigned frames : {3u, 30u, 96u, 4800u, 48000u, 384000u}) {
    std::vector<unsigned char> input(frames * 2), gold(frames * 2 / 3);
    uint32_t state = 1234567;
    for (auto &v : input) {
      state = state * 1664525u + 1013904223u;
      v = state >> 24;
    }
    size_t used = 0;
    assert(!audio_decimate(input.data(), input.size(), gold.data(), gold.size(),
                           &used));
    for (unsigned step : {1u, 147u, 480u, 511u}) {
      Samples result;
      DecimateLive *s = nullptr;
      assert(!decimate_live_open(collect, &result, &s));
      for (unsigned at = 0; at < frames;) {
        unsigned n = std::min(step, frames - at);
        assert(!decimate_live_accept(s, input.data() + at * 2, n * 2));
        at += n;
      }
      if (frames >= 4800)
        assert(!result.bytes.empty());
      assert(!decimate_live_finish(s));
      assert(result.bytes == gold);
      assert(decimate_live_finish(s) == -EALREADY);
      decimate_live_close(s);
    }
  }
  Samples failed;
  failed.fail = true;
  DecimateLive *s = nullptr;
  assert(!decimate_live_open(collect, &failed, &s));
  unsigned char pcm[1920]{};
  assert(decimate_live_accept(s, pcm, sizeof(pcm)) == -EPIPE);
  assert(decimate_live_finish(s) == -EPIPE);
  decimate_live_close(s);
  for (unsigned ms : {100u, 101u, 500u, 8000u}) {
    mode = 0;
    raw48.clear();
    actual16.clear();
    LiveCaptureMetrics m{};
    assert(!audio_capture_live(ms, 5, 0, sink48, sink16, nullptr, cancel,
                               nullptr, &m));
    assert(m.samples48 == ms * 48 && m.samples16 == ms * 16 && !m.ended);
    assert(actual16.size() == ms * 32 && raw48.size() == ms * 96);
    std::vector<unsigned char> gold(actual16.size());
    size_t used;
    assert(!audio_decimate(raw48.data(), raw48.size(), gold.data(), gold.size(),
                           &used));
    assert(gold == actual16 && m.first_pcm16_us >= 0 && m.queue_peak > 0 &&
           m.queue_peak <= 802);
  }
  for (unsigned failure : {1u, 2u, 3u, 4u, 6u}) {
    mode = failure;
    actual16.clear();
    LiveCaptureMetrics m{};
    assert(audio_capture_live(500, 5, mode == 3 ? 400 : 0, nullptr, sink16,
                              nullptr, cancel, nullptr, &m) < 0);
  }
  cancelled = 1;
  mode = 0;
  LiveCaptureMetrics m{};
  assert(audio_capture_live(100, 5, 0, nullptr, sink16, nullptr, cancel,
                            nullptr, &m) == -ECANCELED);
  cancelled = 0;
  mode = 7;
  actual16.clear();
  assert(!audio_capture_live(3000, 5, 400, nullptr, sink16, nullptr, cancel,
                             nullptr, &m));
  assert(m.ended && m.speech && m.samples48 >= 48000 && m.samples48 < 100000);
  assert(m.samples16 == m.samples48 / 3);
  slow_once=true;actual16.clear();
  assert(!audio_capture_live(3000,5,400,nullptr,sink16,nullptr,cancel,nullptr,&m));
  assert(m.ended && m.samples48>=48000 && m.samples48<50000);
  assert(m.samples16==m.samples48/3); // delayed consumer still drains every captured sample
  unsigned before = calls;
  assert(audio_capture_live(99, 5, 0, nullptr, sink16, nullptr, cancel, nullptr,
                            &m) == -EINVAL);
  assert(audio_capture_live(100, 5, 99, nullptr, sink16, nullptr, cancel,
                            nullptr, &m) == -EINVAL);
  assert(calls == before);
  mode=0;
  inject_join_failure=true;
  // This branch intentionally retains the queue until reboot. Do not hide
  // leaks in the normal tests above. The harness joins the real OS thread
  // AFTER observing the public retained-owner result; no firmware retry.
  __lsan_disable();
  int joined=audio_capture_live(100,5,0,nullptr,sink16,nullptr,cancel,nullptr,&m);
  __lsan_enable();
  assert(joined==-EOWNERDEAD && m.consumer_retained);
  assert(!__real_pthread_join(retained_worker,nullptr));
  puts("PASS: injected capture join failure transfers callback lifetime; intentional quarantine retention, not leak-free recovery");
  puts("PASS: live 48->16 FIR byte-exact vs production batch; arbitrary chunk "
       "boundaries/max8s, real worker queue, energy endpoint, "
       "no-speech/cancel/FIFO/sink/short-driver failures, reuse (host mocks; "
       "NOT board ASR)");
}
