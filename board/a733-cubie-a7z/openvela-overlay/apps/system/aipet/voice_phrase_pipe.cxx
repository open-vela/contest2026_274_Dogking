/* SPDX-License-Identifier: Apache-2.0 */
#include "voice_phrase_pipe.hxx"
#ifdef __NuttX__
#include <nuttx/config.h>
#endif
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <new>
#include <pthread.h>
#include <unistd.h>
namespace aipet {
struct VoicePhrasePipe {
  std::atomic<bool> downstream_retained{false};
  char queue[4][769]{}, pending[769]{};
  unsigned head = 0, count = 0, bytes = 0, chars = 0, total = 0;
  unsigned queued = 0, spoken = 0;
  long long start = 0, deadline = 0, first_phrase = 0, first_synthesis = 0;
  bool done = false, joined = false;
  std::atomic<int> error{0};
  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_t thread;
  VoicePhraseSpeak speak;
  VoicePhraseFeed feed=nullptr;
  unsigned produced=0;
  void *cookie;
};
static long long phrase_now() {
  timespec t{};
  if (clock_gettime(CLOCK_MONOTONIC, &t))
    return -(errno ? errno : EIO);
  return t.tv_sec * 1000000LL + t.tv_nsec / 1000;
}
static int phrase_fail(VoicePhrasePipe *s, int rc) {
  int expected = 0;
  s->error.compare_exchange_strong(expected, rc < 0 ? rc : -EIO);
  return s->error.load();
}
static int phrase_cancel(void *p) {
  auto *s = static_cast<VoicePhrasePipe *>(p);
  int rc = s->error.load();
  if (rc)
    return rc;
  long long at = phrase_now();
  if (at < 0)
    return phrase_fail(s, at);
  return at > s->deadline ? phrase_fail(s, -ETIMEDOUT) : 0;
}
static int phrase_queue(VoicePhrasePipe *s) {
  while (s->bytes &&
         (s->pending[s->bytes - 1] == ' ' || s->pending[s->bytes - 1] == '\n' ||
          s->pending[s->bytes - 1] == '\t'))
    s->pending[--s->bytes] = 0;
  if (!s->bytes)
    return 0;
  for (;;) {
    int rc = phrase_cancel(s);
    if (rc)
      return rc;
    pthread_mutex_lock(&s->mutex);
    if (s->count < 4) {
      unsigned at = (s->head + s->count) % 4;
      std::memcpy(s->queue[at], s->pending, s->bytes + 1);
      ++s->count;
      ++s->queued;
      if (!s->first_phrase)
        s->first_phrase = phrase_now() - s->start;
      s->bytes = s->chars = 0;
      s->pending[0] = 0;
      pthread_mutex_unlock(&s->mutex);
      return 0;
    }
    pthread_mutex_unlock(&s->mutex);
    usleep(1000);
  }
}
static int phrase_next(void *p,char *text,std::size_t cap) {
  auto *s=static_cast<VoicePhrasePipe *>(p);
  if(!text || cap<769)return -EINVAL;
  for(;;) {
    int rc=phrase_cancel(s);if(rc)return rc;
    pthread_mutex_lock(&s->mutex);
    if(s->count) {
      std::strcpy(text,s->queue[s->head]);
      std::memset(s->queue[s->head],0,769);
      s->head=(s->head+1)%4;--s->count;++s->produced;
      if(!s->first_synthesis)s->first_synthesis=phrase_now()-s->start;
      pthread_mutex_unlock(&s->mutex);return 1;
    }
    bool done=s->done;pthread_mutex_unlock(&s->mutex);
    if(done)return 0;
    return -EAGAIN;
  }
}
static void *phrase_worker(void *p) {
  auto *s = static_cast<VoicePhrasePipe *>(p);
  if(s->feed) {
    int rc=s->feed(s->cookie,phrase_next,s,phrase_cancel,s);
    if(rc==-EOWNERDEAD)s->downstream_retained.store(true);
    if(rc)phrase_fail(s,rc);else s->spoken=s->produced;
    return nullptr;
  }
  char text[769];
  for (;;) {
    if (phrase_cancel(s))
      return nullptr;
    pthread_mutex_lock(&s->mutex);
    if (!s->count) {
      bool done = s->done;
      pthread_mutex_unlock(&s->mutex);
      if (done)
        return nullptr;
      usleep(1000);
      continue;
    }
    std::strcpy(text, s->queue[s->head]);
    std::memset(s->queue[s->head], 0, 769);
    s->head = (s->head + 1) % 4;
    --s->count;
    if (!s->first_synthesis)
      s->first_synthesis = phrase_now() - s->start;
    pthread_mutex_unlock(&s->mutex);
    int rc = s->speak(s->cookie, text, phrase_cancel, s);
    if (rc) {
      if(rc==-EOWNERDEAD)s->downstream_retained.store(true);
      phrase_fail(s, rc);
      return nullptr;
    }
    ++s->spoken;
  }
}
static int phrase_open(VoicePhraseSpeak speak,VoicePhraseFeed feed, void *cookie,
                      VoicePhrasePipe **out) {
  if (!out)
    return -EINVAL;
  *out = nullptr;
  if (!speak && !feed)
    return -EINVAL;
  auto *s = new (std::nothrow) VoicePhrasePipe;
  if (!s)
    return -ENOMEM;
  s->speak = speak;
  s->feed = feed;
  s->cookie = cookie;
  s->start = phrase_now();
  if (s->start < 0) {
    int rc = s->start;
    pthread_mutex_destroy(&s->mutex);
    delete s;
    return rc;
  }
  s->deadline = s->start + 120000000LL;
  pthread_attr_t attr;
  int rc = pthread_attr_init(&attr);
  if (!rc) {
#ifdef CONFIG_A733_CPU_ACOUSTIC
    rc = pthread_attr_setstacksize(&attr, 262144);
#else
    rc = pthread_attr_setstacksize(&attr, 65536);
#endif
    if (!rc)
      rc = pthread_create(&s->thread, &attr, phrase_worker, s);
    pthread_attr_destroy(&attr);
  }
  if (rc) {
    pthread_mutex_destroy(&s->mutex);
    delete s;
    return -rc;
  }
  *out = s;
  return 0;
}
int voice_phrase_open(VoicePhraseSpeak speak,void *cookie,VoicePhrasePipe **out)
{return phrase_open(speak,nullptr,cookie,out);}
int voice_phrase_open_feed(VoicePhraseFeed feed,void *cookie,VoicePhrasePipe **out)
{return phrase_open(nullptr,feed,cookie,out);}
int voice_phrase_accept(VoicePhrasePipe *s, const char *text,
                        std::size_t size) {
  if (!s || !text || !size)
    return -EINVAL;
  if (s->joined || s->done)
    return -EALREADY;
  if (size > 4096 - s->total)
    return phrase_fail(s, -E2BIG);
  s->total += size;
  for (std::size_t at = 0; at < size;) {
    auto *p = reinterpret_cast<const unsigned char *>(text + at);
    unsigned n = 1, cp = p[0];
    if (cp >= 0xc2 && cp <= 0xdf) {
      n = 2;
      cp &= 31;
    } else if (cp >= 0xe0 && cp <= 0xef) {
      n = 3;
      cp &= 15;
    } else if (cp >= 0xf0 && cp <= 0xf4) {
      n = 4;
      cp &= 7;
    } else if (cp >= 128)
      return phrase_fail(s, -EILSEQ);
    if (n > size - at)
      return phrase_fail(s, -EILSEQ);
    for (unsigned i = 1; i < n; i++) {
      if ((p[i] & 0xc0) != 0x80)
        return phrase_fail(s, -EILSEQ);
      cp = (cp << 6) | (p[i] & 63);
    }
    if ((n == 2 && cp < 128) || (n == 3 && cp < 2048) ||
        (n == 4 && cp < 65536) || cp > 0x10ffff ||
        (cp >= 0xd800 && cp <= 0xdfff) || (cp < 32 && cp != 10 && cp != 9) ||
        cp == 127 || cp == '<' || cp == '>' || cp == '{' || cp == '}' ||
        cp == '[' || cp == ']')
      return phrase_fail(s, -EILSEQ);
    if (!s->bytes &&
        (cp == ' ' || cp == 9 || cp == 10 || cp == 0xa0 || cp == 0x3000)) {
      at += n;
      continue;
    }
    bool punctuation = cp == 0x3002 || cp == 0xff01 || cp == 0xff1f ||
                       cp == '!' || cp == '?' || cp == ';' || cp == 10;
    bool comma_cp = cp == 0xff0c || cp == ',';
    // Only linguistic boundaries may publish a phrase. Character-count
    // cuts split Mandarin compounds and English words mid-pronunciation.
    if (s->bytes + n >= sizeof(s->pending))
      return phrase_fail(s, -E2BIG);
    std::memcpy(s->pending + s->bytes, text + at, n);
    s->bytes += n;
    s->pending[s->bytes] = 0;
    // Estimated spoken cost: English is spelled, not one Chinese syllable
    // per Latin input byte. Keep first audio short without dropping text.
    s->chars+=((cp>='A' && cp<='Z') || (cp>='a' && cp<='z'))?4u:
      (cp>0xffff?16u:1u);
    at += n;
    /* First coherent clause starts speech early. Later clauses keep the old
     * bound. Never omit the remainder or invent text/audio to hit a timer. */
    bool first = s->queued == 0;
    bool comma = comma_cp && s->chars >= (first?8u:16u);
    if (punctuation || comma) {
      int rc = phrase_queue(s);
      if (rc)
        return rc;
    }
  }
  return phrase_cancel(s);
}
int voice_phrase_finish(VoicePhrasePipe *s, int upstream,
                        VoicePhraseMetrics *out) {
  if (!s || !out)
    return -EINVAL;
  *out = {};
  if (s->joined)
    return -EALREADY;
  int rc = upstream ? phrase_fail(s, upstream) : phrase_queue(s);
  pthread_mutex_lock(&s->mutex);
  s->done = true;
  pthread_mutex_unlock(&s->mutex);
  int joined = pthread_join(s->thread, nullptr);
  if (joined)
    return -joined;
  s->joined = true;
  if (!rc)
    rc = s->error.load();
  *out = {s->queued, s->spoken, s->first_phrase, s->first_synthesis};
  return rc;
}
void voice_phrase_close(VoicePhrasePipe *s) {
  if (!s)
    return;
  if (!s->joined) {
    VoicePhraseMetrics m{};
    voice_phrase_finish(s, -ECANCELED, &m);
    if (!s->joined) {
      std::puts("phrase-pipe: join failed; state retained until reboot");
      return;
    }
  }
  if(s->downstream_retained.load()) {
    // The I2S worker may still poll phrase_cancel(s). Retain the complete
    // cancellation chain even though this producer thread has joined.
    std::puts("phrase-pipe: downstream worker retained; cancellation owner retained until reboot");
    return;
  }
  volatile char *p = reinterpret_cast<volatile char *>(s->queue);
  for (unsigned i = 0; i < sizeof(s->queue); i++)
    p[i] = 0;
  p = s->pending;
  for (unsigned i = 0; i < sizeof(s->pending); i++)
    p[i] = 0;
  pthread_mutex_destroy(&s->mutex);
  delete s;
}
} // namespace aipet
