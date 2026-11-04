/* SPDX-License-Identifier: Apache-2.0 */
#include "voice_play_stream.hxx"
#include "audio_resample.hxx"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <new>
#include <pthread.h>
#include <system/a733_pcm.h>
#include <unistd.h>
namespace aipet {
namespace {
constexpr unsigned queue_bytes = 288000;
struct Playback {
  unsigned char ring[queue_bytes];
  unsigned head = 0, used = 0, frames = 0, volume;
  unsigned limit=480000, segment_frames=0;
  bool done = false, started = false;
  bool gap_open = false;
  unsigned segments = 0, rebuffers = 0;
  long long gap_start = 0, rebuffer_us = 0;
  int error = 0;
  long long start = 0, deadline = 0, first_pcm = 0, first_pull = 0;
  VoiceLocalCancel cancel;
  void *cookie;
  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  ~Playback() {
    volatile unsigned char *p = ring;
    for (unsigned i = 0; i < queue_bytes; i++)
      p[i] = 0;
    pthread_mutex_destroy(&mutex);
  }
};
long long now() {
  timespec t{};
  if (clock_gettime(CLOCK_MONOTONIC, &t))
    return -(errno ? errno : EIO);
  return t.tv_sec * 1000000LL + t.tv_nsec / 1000;
}
int fail(Playback *s, int error) {
  pthread_mutex_lock(&s->mutex);
  if (!s->error)
    s->error = error < 0 ? error : -EIO;
  int rc = s->error;
  pthread_mutex_unlock(&s->mutex);
  return rc;
}
int poll(void *p) {
  auto *s = static_cast<Playback *>(p);
  long long at = now();
  if (at < 0)
    return fail(s, static_cast<int>(at));
  if (at > s->deadline)
    return fail(s, -ETIMEDOUT);
  int rc = s->cancel ? s->cancel(s->cookie) : 0;
  if (rc)
    return fail(s, rc < 0 ? rc : -ECANCELED);
  pthread_mutex_lock(&s->mutex);
  rc = s->error;
  pthread_mutex_unlock(&s->mutex);
  return rc;
}
int push(void *p, const unsigned char *data, std::size_t bytes) {
  auto *s = static_cast<Playback *>(p);
  if (!data || !bytes || bytes > 960 || bytes % 2)
    return fail(s, -EINVAL);
  for (;;) {
    int rc = poll(s);
    if (rc)
      return rc;
    pthread_mutex_lock(&s->mutex);
    if (queue_bytes - s->used >= bytes) {
      unsigned tail = (s->head + s->used) % queue_bytes;
      unsigned n = queue_bytes - tail;
      if (n > bytes)
        n = bytes;
      std::memcpy(s->ring + tail, data, n);
      std::memcpy(s->ring, data + n, bytes - n);
      s->used += bytes;
      if (!s->first_pcm)
        s->first_pcm = now() - s->start;
      pthread_mutex_unlock(&s->mutex);
      return 0;
    }
    pthread_mutex_unlock(&s->mutex);
    usleep(1000);
  }
}
int read(void *p, unsigned char *data, uint32_t cap, uint32_t *bytes) {
  auto *s = static_cast<Playback *>(p);
  if (!data || !bytes || cap < 2)
    return -EINVAL;
  *bytes = 0;
  for (;;) {
    int rc = poll(s);
    if (rc)
      return rc;
    pthread_mutex_lock(&s->mutex);
    if(s->segment_frames==480000) {
      pthread_mutex_unlock(&s->mutex);return 0;
    }
    if(s->used && s->frames+s->segment_frames>=s->limit) {
      pthread_mutex_unlock(&s->mutex);return fail(s,-EOVERFLOW);
    }
    if (!s->used) {
      /* End THIS synchronous device segment, not the utterance. Its driver
       * drains and quiets TX before the next read may wait for the producer.
       * Never sleep waiting for CPU acoustic synthesis with live PIO TX.
       */
      if (!s->done && s->started && !s->gap_open) {
        s->gap_open = true;
        s->gap_start = now();
        ++s->rebuffers;
      }
      pthread_mutex_unlock(&s->mutex);
      return 0;
    }
    if (s->gap_open) {
      s->rebuffer_us += now() - s->gap_start;
      s->gap_open = false;
    }
    unsigned n = s->used;
    unsigned remaining=(480000-s->segment_frames)*2;
    if(n>remaining)n=remaining;
    if (n > cap)
      n = cap;
    n &= ~1u;
    unsigned first = queue_bytes - s->head;
    if (first > n)
      first = n;
    std::memcpy(data, s->ring + s->head, first);
    std::memcpy(data + first, s->ring, n - first);
    s->head = (s->head + n) % queue_bytes;
    s->used -= n;
    s->segment_frames+=n/2;
    *bytes = n;
    if (!s->started) {
      s->started = true;
      s->first_pull = now() - s->start;
    }
    pthread_mutex_unlock(&s->mutex);
    return 0;
  }
}
void *worker(void *p) {
  auto *s = static_cast<Playback *>(p);
  for (;;) {
    /* Wait BEFORE opening a device transaction. Only this thread removes
     * queued PCM, so data cannot disappear between this check and read().
     * This also avoids an empty final ioctl whose ENODATA/close error would
     * otherwise be indistinguishable. All device errors remain fatal.
     */
    int rc = poll(s);
    if (rc)
      break;
    pthread_mutex_lock(&s->mutex);
    bool complete = s->done && s->used == 0;
    // First start: 40ms of real PCM. After an underrun keep the established
    // 100ms rebuffer gate; no silence/padding and no empty transaction.
    bool ready = s->used >= (s->started ? 9600u : 3840u) ||
                 (s->done && s->used != 0);
    if (complete && s->gap_open) {
      s->rebuffer_us += now() - s->gap_start;
      s->gap_open = false;
    }
    pthread_mutex_unlock(&s->mutex);
    if (complete) {
      if (!s->frames)
        fail(s, -ENODATA);
      break;
    }
    if (!ready) {
      usleep(1000);
      continue;
    }
    uint32_t frames = 0;
    s->segment_frames=0;
    rc = a733_pcm_stream(read, s, s->volume, &frames);
    if (frames > s->limit - s->frames) {
      fail(s, -EOVERFLOW);
      break;
    }
    s->frames += frames;
    if (frames)
      ++s->segments;
    if (rc) {
      fail(s, rc);
      break;
    }
    if (!frames) {
      fail(s, -EIO);
      break;
    }
  }
  return nullptr;
}
int resample(void *p, const unsigned char *data, std::size_t bytes) {
  return matcha_pcm_stream_accept(static_cast<MatchaPcmStream *>(p), data,
                                  bytes);
}
} // namespace
static int play(const char *text,VoiceTextNext next,void *source,unsigned volume,
                      VoiceLocalCancel cancel, void *cookie,
                      VoicePlayMetrics *out) {
  if (out)
    *out = {};
  if ((!text && !next) || !out || volume > 100)
    return -EINVAL;
  if (!(voice_local_capabilities() & 2))
    return -ENODEV;
  if (!volume)
    return 0;
  auto *s = new (std::nothrow) Playback;
  if (!s)
    return -ENOMEM;
  s->volume = volume;
  s->limit=2880000;
  s->cancel = cancel;
  s->cookie = cookie;
  s->start = now();
  if (s->start < 0) {
    int rc = s->start;
    delete s;
    return rc;
  }
  s->deadline = s->start + 120000000LL;
  MatchaPcmStream *converter = nullptr;
  int rc = matcha_pcm_stream_open_limit(push, s, &converter,1323000);
  if (rc) {
    delete s;
    return rc;
  }
  pthread_t task;
  pthread_attr_t attr;
  rc = pthread_attr_init(&attr);
  if (!rc) {
    rc = pthread_attr_setstacksize(&attr, 32768);
    if (!rc)
      rc = pthread_create(&task, &attr, worker, s);
    pthread_attr_destroy(&attr);
  }
  if (rc) {
    matcha_pcm_stream_close(converter);
    delete s;
    return -rc;
  }
  if(next) {
    char clause[769];unsigned clauses=0;
    for(;;) {
      rc=poll(s);if(rc)break;
      int status=next(source,clause,sizeof(clause));
      if(status==-EAGAIN){usleep(1000);continue;}
      if(status<=0){rc=status;break;}
      if(status!=1 || !memchr(clause,0,sizeof(clause)) || !*clause || ++clauses>64) {
        rc=-EILSEQ;break;
      }
      rc=voice_local_speak_stream(clause,resample,converter,poll,s);
      if(rc)break;
    }
  } else rc = voice_local_speak_stream(text, resample, converter, poll, s);
  if (!rc)
    rc = matcha_pcm_stream_finish(converter);
  if (rc)
    fail(s, rc);
  pthread_mutex_lock(&s->mutex);
  s->done = true;
  pthread_mutex_unlock(&s->mutex);
  int joined = pthread_join(task, nullptr);
  matcha_pcm_stream_close(converter);
  if (joined) {
    fail(s,-EOWNERDEAD);
    out->worker_retained=true;
    std::puts("local-stream: join failed; PCM queue retained until reboot");
    return -EOWNERDEAD;
  }
  if (!rc)
    rc = s->error;
  *out = {s->first_pcm, s->first_pull, s->frames,s->start,
          s->first_pull>0?s->start+s->first_pull:0};
  out->device_segments = s->segments;
  out->rebuffers = s->rebuffers;
  out->rebuffer_wait_us = s->rebuffer_us;
  delete s;
  return rc;
}
int voice_play_stream(const char *text,unsigned volume,VoiceLocalCancel cancel,
    void *cookie,VoicePlayMetrics *out)
{return play(text,nullptr,nullptr,volume,cancel,cookie,out);}
int voice_play_feed(VoiceTextNext next,void *source,unsigned volume,VoiceLocalCancel cancel,
    void *cookie,VoicePlayMetrics *out)
{return play(nullptr,next,source,volume,cancel,cookie,out);}
} // namespace aipet
