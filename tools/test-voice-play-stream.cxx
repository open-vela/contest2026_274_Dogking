/* SPDX-License-Identifier: Apache-2.0 */
#include "audio_resample.hxx"
#include "voice_play_stream.hxx"
#include <atomic>
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
static std::atomic<bool> finished{false};
static std::atomic<bool> force_cancel{false};
static std::atomic<bool> first_pull_seen{false};
static bool early = false;
static int mode = 0;
static unsigned calls = 0;
static unsigned empty_transactions = 0;
static std::vector<unsigned char> played;
extern "C" int a733_pcm_stream(a733_pcm_reader read, void *p, unsigned volume,
                               uint32_t *frames) {
  assert(volume == 50);
  *frames = 0;
  if (mode == 2)
    return -ENODEV;
  if (mode == 8)
    return 0; // Broken adapter: no read and no progress must not busy-loop.
  if (mode == 10)
    return -ENODATA; // A device error must NEVER be normalized to success.
  unsigned char block[960];
  ++calls;
  for (;;) {
    uint32_t n = 0;
    int rc = read(p, block, sizeof(block), &n);
    if (rc)
      return rc;
    if (!n) {
      if (mode == 11 && *frames)
        return -EIO; // Device close/finalization fails after valid audio.
      if (!*frames)
        ++empty_transactions;
      if (mode == 7 && !finished)
        force_cancel = true;
      return *frames ? 0 : -ENODATA; // Real driver's empty-initial-read contract.
    }
    if (!finished)
      early = true;
    first_pull_seen = true;
    played.insert(played.end(), block, block + n);
    *frames += n / 2;
    if (mode == 3)
      return -EPIPE;
    usleep(10000); // Model a 10ms PCM device, not host memory-speed playback.
  }
}
static int synth(void *, const char *, VoiceLocalPcmSink sink, void *p,
                 long long, VoiceLocalCancel cancel, void *cookie) {
  finished = false;
  unsigned char block[4410];
  for (unsigned i = 0; i < sizeof(block) / 2; i++) {
    int16_t v = (i % 100) * 100;
    block[i * 2] = v & 255;
    block[i * 2 + 1] = uint16_t(v) >> 8;
  }
  for (unsigned i = 0; i < (mode==13?110u:10u); i++) {
    int rc = cancel(cookie);
    if (rc)
      return rc;
    if (mode == 12 && i == 0) {
      // 50ms source PCM is enough for v211's 40ms initial gate but NOT
      // the old 100ms gate. Keep the exact original samples/order.
      rc = sink(p, block, 2206);
      if (rc) return rc;
      for (unsigned wait = 0; wait < 50 && !first_pull_seen; ++wait)
        usleep(10000);
      if (!first_pull_seen) return -ETIMEDOUT;
      rc = sink(p, block + 2206, sizeof(block) - 2206);
      if (rc) return rc;
      continue;
    }
    rc = sink(p, block, mode == 9 ? 882 : sizeof(block));
    if (rc)
      return rc;
    if (mode == 1 && i == 1)
      return -EIO;
    if ((mode == 4 || mode == 6 || mode == 7) && i == 1) {
      usleep(400000);
      rc = cancel(cookie);
      if (rc)
        return rc;
      if (mode == 6)
        return -EIO;
    }
    if (mode == 9)
      break;
  }
  if (mode == 5)
    usleep(1300000); // Last PCM can drain before provider/model close finishes.
  finished = true;
  return 0;
}
static int cancel(void *) { return force_cancel ? -ECANCELED : 0; }
int main() {
  VoicePlayMetrics m{};
  assert(voice_play_stream("你好", 50, cancel, nullptr, &m) == -ENODEV &&
         !calls);
  VoiceLocalProvider provider{
      "host-mock-not-hardware", 0, 1, 1, nullptr, nullptr, synth};
  assert(!voice_local_install(&provider));
  assert(!voice_play_stream("你好", 50, cancel, nullptr, &m));
  assert(early && m.frames == 48000 && played.size() == 96000 &&
         m.first_pcm_us > 0 && m.first_pull_us >= m.first_pcm_us);
  const auto reference = played;
  mode=13;played.clear();
  assert(!voice_play_stream("单次英文拼读和诊断超过十秒",50,cancel,nullptr,&m));
  assert(m.frames==11*48000 && played.size()==11*96000);
  puts("PASS v221fix1: ONE provider call, complete11s PCM across the old10s cap (host mock)");
  mode=0;
  // A single reply may exceed the old per-call 10s bound. All 12 one-second
  // source clauses share one resampling phase and one playback owner.
  unsigned clauses=0;mode=0;played.clear();
  auto next=[](void *p,char *out,std::size_t cap){
    auto &n=*static_cast<unsigned *>(p);assert(cap>=769);
    if(n++==12)return 0;
    std::strcpy(out,"完整的一句。");return 1;
  };
  assert(!voice_play_feed(next,&clauses,50,cancel,nullptr,&m));
  assert(m.frames==12*48000 && played.size()==12*96000);
  puts("PASS: one reply feed, 12 clauses/seconds, exact frame count across 10s device boundary (host mock)");
  mode = 12; played.clear(); first_pull_seen = false;
  assert(!voice_play_stream("四十毫秒首次起播门槛", 50, cancel, nullptr, &m));
  assert(first_pull_seen && m.frames == 48000 && played == reference);
  puts("PASS: v211 starts on 50ms source PCM before next producer block, exact complete audio");
  mode = 4; played.clear();
  int starved = voice_play_stream("你好，语音测试成功。", 50, cancel, nullptr, &m);
  std::printf("producer gap regression: result=%d frames=%u bytes=%zu\n",
              starved, m.frames, played.size());
  std::fflush(stdout);
  assert(starved == 0 && m.frames == 48000 && played == reference);
  assert(m.rebuffers >= 1 && m.device_segments >= 2 && m.rebuffer_wait_us > 0);
  mode = 5; played.clear();
  assert(!voice_play_stream("模型关闭晚于PCM排空", 50, cancel, nullptr, &m));
  assert(m.frames == 48000 && played == reference);
  mode = 9; played.clear();
  assert(!voice_play_stream("短尾帧", 50, cancel, nullptr, &m));
  assert(m.frames == 960 && played.size() == 1920);
  assert(empty_transactions == 0); // Initial buffering/late close occur outside ioctl.
  mode = 0;
  for (int failure : {1, 2, 3, 6, 7, 8, 10, 11}) {
    mode = failure;
    force_cancel = false;
    played.clear();
    int rc = voice_play_stream("失败恢复", 50, cancel, nullptr, &m);
    assert(rc < 0);
    if (failure == 7)
      assert(rc == -ECANCELED);
    if (failure == 10)
      assert(rc == -ENODATA);
    if (failure == 11)
      assert(rc == -EIO);
    mode = 0;
    force_cancel = false;
    played.clear();
    assert(!voice_play_stream("恢复", 50, cancel, nullptr, &m));
  }
  inject_join_failure=true;
  __lsan_disable();
  int retained=voice_play_stream("异常所有权",50,cancel,nullptr,&m);
  __lsan_enable();
  assert(retained==-EOWNERDEAD && m.worker_retained);
  assert(!__real_pthread_join(retained_worker,nullptr));
  puts("PASS: injected playback join failure reports retained cancellation owner; intentional quarantine retention");
  assert(!voice_local_install(nullptr));
  puts("PASS: playback before synthesis completion; bounded producer/consumer, "
       "lossless producer gaps/late close/short tails, real device/provider/cancel "
       "errors and reusable cleanup (host mocks)");
}
