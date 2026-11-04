/* SPDX-License-Identifier: Apache-2.0 */
#include "audio_capture_live.hxx"
#include "voice_compute.hxx"
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <climits>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <new>
#include <pthread.h>
#include <system/a733_pcm.h>
#include <unistd.h>
namespace aipet {
struct DecimateLive {
  double taps[63], sum = 0;
  int16_t ring[128]{};
  unsigned input = 0, output = 0, used = 0;
  unsigned char pcm[320];
  LiveSink sink;
  void *cookie;
  int error = 0;
  bool done = false;
};
static int send16(DecimateLive *s) {
  if (!s->used)
    return 0;
  int rc = s->sink(s->cookie, s->pcm, s->used);
  s->used = 0;
  return s->error = rc ? (rc < 0 ? rc : -EIO) : 0;
}
static int produce16(DecimateLive *s, bool finish) {
  while (s->output < s->input / 3) {
    unsigned base = s->output * 3;
    if (!finish && base + 31 >= s->input)
      break;
    double value = 0;
    for (int k = 0; k < 63; k++) {
      int at = static_cast<int>(base) + k - 31;
      if (at >= 0 && static_cast<unsigned>(at) < s->input)
        value += s->ring[at % 128] * s->taps[k] / s->sum;
    }
    long v = std::lround(value);
    if (v > 32767)
      v = 32767;
    if (v < -32768)
      v = -32768;
    uint16_t raw = static_cast<uint16_t>(v);
    s->pcm[s->used++] = raw & 255;
    s->pcm[s->used++] = raw >> 8;
    ++s->output;
    if (s->used == sizeof(s->pcm) && send16(s))
      return s->error;
  }
  return 0;
}
int decimate_live_open(LiveSink sink, void *cookie, DecimateLive **out) {
  if (!out)
    return -EINVAL;
  *out = nullptr;
  if (!sink)
    return -EINVAL;
  auto *s = new (std::nothrow) DecimateLive;
  if (!s)
    return -ENOMEM;
  s->sink = sink;
  s->cookie = cookie;
  constexpr double pi = 3.14159265358979323846, cutoff = 6500.0 / 48000.0;
  for (int i = 0; i < 63; i++) {
    int k = i - 31;
    double sinc = k ? std::sin(2 * pi * cutoff * k) / (pi * k) : 2 * cutoff;
    s->taps[i] = sinc * (0.54 - 0.46 * std::cos(2 * pi * i / 62));
    s->sum += s->taps[i];
  }
  *out = s;
  return 0;
}
int decimate_live_accept(DecimateLive *s, const unsigned char *pcm,
                         std::size_t bytes) {
  if (!s)
    return -EINVAL;
  if (s->error)
    return s->error;
  if (s->done)
    return -EALREADY;
  if (!pcm || !bytes || bytes % 2 || bytes / 2 > 384000 - s->input)
    return s->error = -EINVAL;
  for (std::size_t i = 0; i < bytes; i += 2) {
    unsigned raw = pcm[i] | (unsigned(pcm[i + 1]) << 8);
    s->ring[s->input % 128] = raw >= 32768 ? int(raw) - 65536 : int(raw);
    ++s->input;
    if (produce16(s, false))
      return s->error;
  }
  return 0;
}
int decimate_live_finish(DecimateLive *s) {
  if (!s)
    return -EINVAL;
  if (s->error)
    return s->error;
  if (s->done)
    return -EALREADY;
  if (!s->input || s->input % 3)
    return s->error = -EINVAL;
  int rc = produce16(s, true);
  if (!rc)
    rc = send16(s);
  if (!rc)
    s->done = true;
  return rc;
}
void decimate_live_close(DecimateLive *s) {
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
namespace {
constexpr unsigned slots = 802;
struct Block {
  unsigned bytes;
  unsigned char pcm[960];
};
struct Capture {
  Block queue[slots];
  std::atomic<unsigned> written{0}, read{0};
  std::atomic<int> error{0};
  std::atomic<bool> done{false}, endpoint{false};
  LiveSink sink48, sink16;
  void *cookie;
  int (*cancel)(void *);
  void *cancel_cookie;
  unsigned threshold = 0, peak = 0, samples48 = 0, samples16 = 0, attack = 0,
           quiet = 0, accepted48 = 0, limit48 = 0;
  unsigned endpoint_samples=0;
  uint64_t endpoint_energy=0;
  bool speech = false;
  long long start = 0, deadline = 0, first16 = 0;
  DialogueGate *gate=nullptr;
  std::atomic<bool> ready{false},triggered{false};
  std::atomic<uint64_t> monitor_blocks{0},monitor_energy{0},monitor_noise{0};
  std::atomic<unsigned> monitor_peak{0};
  long long next_idle_report=0;
  LiveVoiceState state=nullptr;void *state_cookie=nullptr;
  bool ready_reported=false,speech_reported=false;
  ~Capture() {
    volatile unsigned char *p =
        reinterpret_cast<volatile unsigned char *>(queue);
    for (unsigned i = 0; i < sizeof(queue); i++)
      p[i] = 0;
  }
};
long long now_live() {
  timespec t{};
  if (clock_gettime(CLOCK_MONOTONIC, &t))
    return -EIO;
  return t.tv_sec * 1000000LL + t.tv_nsec / 1000;
}
int fail_live(Capture *s, int rc) {
  int expected = 0;
  s->error.compare_exchange_strong(expected, rc < 0 ? rc : -EIO);
  return s->error.load();
}
int receive_live(void *p, const unsigned char *pcm, uint32_t bytes) {
  auto *s = static_cast<Capture *>(p);
  if (s->error.load())
    return s->error.load();
  int rc = s->cancel ? s->cancel(s->cancel_cookie) : 0;
  if (rc)
    return fail_live(s, rc < 0 ? rc : -ECANCELED);
  long long at = now_live();
  if (at < 0)
    return fail_live(s, at);
  if (at > s->deadline)
    return fail_live(s, -ETIMEDOUT);
  if (!pcm || !bytes || bytes > 960 || bytes % 6)
    return fail_live(s, -EINVAL);
  if (bytes / 2 > s->limit48 - s->accepted48)
    return fail_live(s, -EMSGSIZE);
  unsigned n = s->written.load(), r = s->read.load(std::memory_order_acquire);
  if (n - r >= slots)
    return fail_live(s, -ENOBUFS);
  Block &b = s->queue[n % slots];
  b.bytes = bytes;
  std::memcpy(b.pcm, pcm, bytes);
  // Real-time capture-side 10ms raw-PCM energy endpoint. The NPU consumer
  // may lag by seconds: it must NOT delay the decision to stop recording.
  // This is a diagnostic energy gate, not trained VAD/AEC; ASR still has to
  // produce valid nonempty text before any LLM call. No samples are dropped.
  if(s->threshold)for(unsigned i=0;i<bytes;i+=2) {
    unsigned raw=pcm[i]|(unsigned(pcm[i+1])<<8);
    int v=raw>=32768?int(raw)-65536:int(raw);
    s->endpoint_energy+=int64_t(v)*v;
    if(++s->endpoint_samples==480) {
      bool active=s->endpoint_energy>=uint64_t(s->threshold)*s->threshold*480;
      if(active){++s->attack;s->quiet=0;if(s->attack>=3)s->speech=true;}
      else {s->attack=0;if(s->speech)++s->quiet;}
      if(s->speech && s->accepted48+ i/2+1>=14400 && s->quiet>=50)
        s->endpoint.store(true);
      s->endpoint_energy=0;s->endpoint_samples=0;
    }
  }
  if (n - r + 1 > s->peak)
    s->peak = n - r + 1;
  s->accepted48 += bytes / 2;
  s->written.store(n + 1, std::memory_order_release);
  return s->endpoint.load() ? 1 : 0;
}
int emit_live16(void *p, const unsigned char *pcm, std::size_t bytes) {
  auto *s = static_cast<Capture *>(p);
  if (!s->first16)
    s->first16 = now_live() - s->start;
  s->samples16 += bytes / 2;
  int rc = s->sink16(s->cookie, pcm, bytes);
  return rc ? fail_live(s, rc) : 0;
}
void *consume_live(void *p) {
  auto *s = static_cast<Capture *>(p);
  DecimateLive *converter = nullptr;
  int rc = decimate_live_open(emit_live16, s, &converter);
  if (rc) {
    fail_live(s, rc);
    return nullptr;
  }
  for (;;) {
    if(s->gate) {
      if(s->ready.load() && !s->ready_reported) {
        s->ready_reported=true;if(s->state)s->state(s->state_cookie,1);
      }
      if(s->triggered.load() && !s->speech_reported) {
        s->speech_reported=true;if(s->state)s->state(s->state_cookie,2);
      }
      // Diagnostics live on the consumer, NEVER in the timing-sensitive PIO
      // callback. No PCM is stored or uploaded while waiting for speech.
      long long at=now_live();
      if(s->ready.load() && !s->triggered.load() && at>=s->next_idle_report) {
        s->next_idle_report=at+5000000;
        uint64_t noise=s->monitor_noise.load(),energy=s->monitor_energy.load();
        uint64_t base=uint64_t(s->gate->floor)*s->gate->floor*480;
        uint64_t trigger=noise*9>base?noise*9:base;
        std::printf("mic-monitor-v219: idle-ms=%lld blocks=%llu rms=%u raw-peak=%u noise-rms=%u trigger-rms=%u; waiting for speech, no PCM saved\n",
          (at-s->start)/1000,static_cast<unsigned long long>(s->monitor_blocks.load()),
          unsigned(std::sqrt(double(energy)/480)),s->monitor_peak.load(),
          unsigned(std::sqrt(double(noise)/480)),unsigned(std::sqrt(double(trigger)/480)));
        std::fflush(stdout);
      }
    }
    if (s->error.load())
      break;
    long long at = now_live();
    if (at < 0 || at > s->deadline) {
      fail_live(s, at < 0 ? int(at) : -ETIMEDOUT);
      break;
    }
    unsigned n = s->read.load(), w = s->written.load(std::memory_order_acquire);
    if (n == w) {
      if (s->done.load() && n == s->written.load(std::memory_order_acquire))
        break;
      usleep(1000);
      continue;
    }
    Block &b = s->queue[n % slots];
    if (s->sink48)
      rc = s->sink48(s->cookie, b.pcm, b.bytes);
    if (!rc)
      rc = decimate_live_accept(converter, b.pcm, b.bytes);
    if (rc) {
      fail_live(s, rc);
      break;
    }
    s->samples48 += b.bytes / 2;
    s->read.store(n + 1, std::memory_order_release);
  }
  if (!s->error.load()) {
    rc = decimate_live_finish(converter);
    if (rc)
      fail_live(s, rc);
    if (s->threshold && !s->speech)
      fail_live(s, -ENODATA);
  }
  decimate_live_close(converter);
  return nullptr;
}
} // namespace
int audio_capture_live(unsigned ms, unsigned gain, unsigned threshold,
                       LiveSink sink48, LiveSink sink16, void *cookie,
                       int (*cancel)(void *), void *cancel_cookie,
                       LiveCaptureMetrics *out) {
  if (!out)
    return -EINVAL;
  *out = {};
  if (ms < 100 || ms > 8000 || gain < 1 || gain > 16 || !sink16 ||
      (threshold && (threshold < 100 || threshold > 5000)))
    return -EINVAL;
  auto *s = new (std::nothrow) Capture;
  if (!s)
    return -ENOMEM;
  s->sink48 = sink48;
  s->sink16 = sink16;
  s->cookie = cookie;
  s->cancel = cancel;
  s->cancel_cookie = cancel_cookie;
  s->threshold = threshold;
  s->limit48 = ms * 48u;
  s->start = now_live();
  if (s->start < 0) {
    delete s;
    return -EIO;
  }
  s->deadline = s->start + 120000000LL;
  pthread_attr_t attr;
  pthread_t worker;
  int rc = pthread_attr_init(&attr);
  if (!rc) {
    rc = pthread_attr_setstacksize(&attr, 65536);
    if (!rc)
      rc = pthread_create(&worker, &attr, consume_live, s);
    pthread_attr_destroy(&attr);
  }
  if (rc) {
    delete s;
    return -rc;
  }
  uint32_t frames = 0;
  unsigned ended = 0;
  rc = a733_pcm_capture_stream(ms, gain, receive_live, s, &frames, &ended);
  const long long capture_stopped = now_live(); // before ASR consumer backlog drains
  if(rc)std::printf("live-capture-v213: driver-status=%d consumer-status=%d frames=%u written=%u read=%u queue-peak=%u; failed turn is NOT published\n",
    rc,s->error.load(),frames,s->written.load(),s->read.load(),s->peak);
  if (rc)
    fail_live(s, rc);
  s->done.store(true);
  int joined = pthread_join(worker, nullptr);
  if (joined) {
    fail_live(s,-EOWNERDEAD);
    out->consumer_retained=true;
    out->capture_stopped_us=capture_stopped;
    return -EOWNERDEAD; // caller must retain the callback owner, not just this queue
  }
  if (!rc)
    rc = s->error.load();
  if (!rc &&
      (s->samples48 != frames || s->samples16 != frames / 3 ||
       s->written.load() != s->read.load() || (!ended && frames != ms * 48u)))
    rc = -EIO;
  *out = {s->samples48, s->samples16,         s->peak, bool(ended), s->speech,
          s->first16,   now_live() - s->start, capture_stopped, now_live()};
  delete s;
  return rc < 0 ? rc : rc ? -EIO : 0;
}

int audio_capture_dialogue(unsigned gain,unsigned floor,LiveSink sink,void *cookie,
    int (*cancel)(void *),void *cc,LiveVoiceState state,void *sc,LiveCaptureMetrics *out,uint64_t *noise_profile) {
#if !defined(__NuttX__) && !defined(AIPET_TEST_VOICE_MONITOR)
  (void)gain;(void)floor;(void)sink;(void)cookie;(void)cancel;(void)cc;
  (void)state;(void)sc;(void)noise_profile;if(out)*out={};return -ENOTSUP;
#else
  if(!out || !sink || !cancel || gain<1 || gain>16 || floor<100 || floor>5000)return -EINVAL;
  *out={};
  auto *s=new(std::nothrow) Capture;auto *gate=new(std::nothrow) DialogueGate;
  if(!s || !gate){delete s;delete gate;return -ENOMEM;}
  gate->floor=floor;s->gate=gate;s->sink48=nullptr;s->sink16=sink;s->cookie=cookie;
  // Only a previously qualified idle noise floor is reused; no previous PCM
  // or speech/endpoint state crosses a turn. The optional pointer is not
  // retained by either callback, including the uncertain-join branch.
  if(noise_profile && *noise_profile!=UINT64_MAX) {
    if(*noise_profile>uint64_t(32768)*32768*480){delete gate;delete s;return -EINVAL;}
    gate->noise=*noise_profile;gate->ready=true;
  }
  s->cancel=cancel;s->cancel_cookie=cc;s->limit48=384000;s->start=now_live();
  s->deadline=LLONG_MAX;s->state=state;s->state_cookie=sc;s->next_idle_report=s->start+5000000;
  if(s->start<0){delete gate;delete s;return -EIO;}
  pthread_attr_t attr;pthread_t worker;
  int rc=pthread_attr_init(&attr);
  if(!rc){rc=pthread_attr_setstacksize(&attr,65536);if(!rc)rc=pthread_create(&worker,&attr,consume_live,s);pthread_attr_destroy(&attr);}
  if(rc){delete gate;delete s;return -rc;}
  auto receive=[](void *p,const unsigned char *pcm,uint32_t n)->int {
    auto *capture=static_cast<Capture *>(p);
    if(capture->error.load())return capture->error.load();
    int cancelled=capture->cancel(capture->cancel_cookie);if(cancelled)return cancelled;
    auto emit=[](void *c,const unsigned char *b,std::size_t bytes)->int {
      return receive_live(c,b,static_cast<uint32_t>(bytes));
    };
    int r=capture->gate->push(pcm,n,emit,capture);
    ++capture->monitor_blocks;
    capture->monitor_energy=capture->gate->last_energy;
    capture->monitor_noise=capture->gate->ready?capture->gate->noise:0;
    capture->monitor_peak=capture->gate->last_peak;
    capture->ready.store(capture->gate->ready);capture->triggered.store(capture->gate->speech);
    return r;
  };
  uint64_t frames=0;unsigned ended=0;
  // Only the PIO owner occupies CPU1/A55; the consumer was created BEFORE
  // this scoped binding and qualifies CPU6 for NPU work independently.
  VoiceCompute rx_cpu(1,0xd05);rc=rx_cpu.begin();
  if(!rc)rc=a733_pcm_monitor(gain,receive,s,&frames,&ended);
  long long stopped=now_live();
  if(state)state(sc,3); // Hardware has stopped, even while the ASR queue drains.
  rc=rx_cpu.finish(rc);
  if(rc)fail_live(s,rc);
  s->done.store(true);
  int joined=pthread_join(worker,nullptr);
  if(joined){out->consumer_retained=true;out->capture_stopped_us=stopped;return -EOWNERDEAD;}
  if(!rc)rc=s->error.load();
  if(!rc && (!ended || !gate->ended || s->samples48!=gate->emitted*480u ||
      s->samples16!=s->samples48/3 || s->read.load()!=s->written.load()))rc=-EIO;
  out->samples48=s->samples48;out->samples16=s->samples16;out->queue_peak=s->peak;
  out->ended=ended;out->speech=gate->speech;out->limited=gate->limited;out->monitor_frames=frames;
  out->first_pcm16_us=s->first16;out->elapsed_us=now_live()-s->start;
  out->capture_stopped_us=stopped;out->consumer_finished_us=now_live();
  std::printf("dialogue-rx-v214: result=%d monitored=%llu accepted48=%u speech=%u limited=%u queue=%u; idle PCM not saved\n",
    rc,static_cast<unsigned long long>(frames),s->samples48,gate->speech,gate->limited,s->peak);
  std::printf("gate-end-v221fix1: noise-rms=%u last-rms=%u voiced-blocks=%u quiet-blocks=%u; 10ms blocks,700ms endpoint,8s hard discard\n",
    unsigned(std::sqrt(double(gate->noise)/480)),unsigned(std::sqrt(double(gate->last_energy)/480)),gate->voiced,gate->quiet);
  std::printf("gate-end-v224: continuation-confirm=80ms peak-blocks=%u rejected-short-bursts=%u quiet-peak=%u resets=%u limited=%u; energy-only, NOT trained VAD\n",
    gate->continuation_peak,gate->rejected_bursts,gate->quiet_peak,gate->endpoint_resets,gate->limited);
  std::fflush(stdout);
  if(!rc && noise_profile)*noise_profile=gate->noise;
  volatile unsigned char *wipe=&gate->ring[0][0];for(unsigned i=0;i<sizeof(gate->ring);i++)wipe[i]=0;
  delete gate;delete s;return rc;
#endif
}
} // namespace aipet
