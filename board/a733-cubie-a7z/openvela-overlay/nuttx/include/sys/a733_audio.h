/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __INCLUDE_SYS_A733_AUDIO_H
#define __INCLUDE_SYS_A733_AUDIO_H

#include <stdint.h>
#include <sys/ioctl.h>

/* Bounded, synchronous board PCM ABI for CONFIG_BUILD_FLAT. Not the standard
 * queued audio lower-half. Buffers remain caller-owned until ioctl returns.
 * No filesystem access or implicit cloud request. Busy returns -EBUSY.
 */
#define A733_AUDIO_ABI 1u
#define A733_AUDIO_CAPTURE 0x10e0
#define A733_AUDIO_PLAY    0x10e1
#define A733_AUDIO_FILE    0x10e2
#define A733_AUDIO_STREAM  0x10e3
#define A733_AUDIO_LIVE_CAPTURE 0x10e4
#define A733_AUDIO_MONITOR 0x10e5
/* Explicit FLAT-only continuous RX. No automatic/background activation.
 * Same nonblocking 10ms callback contract; caller MUST implement cancel.
 * Unlike LIVE_CAPTURE, idle duration has no fixed 8s boundary. Hardware FIFO
 * waits/counter proof remain bounded; only callback 1 ends a valid recording. */
struct a733_audio_monitor_s
{
  uint32_t version,gain;
  int (*consume)(void *,const uint8_t *,uint32_t);
  void *cookie;
  uint64_t frames;
  uint32_t ended;
};
/* A 10ms capture callback may only copy into a bounded RAM queue. No NPU,
 * filesystem, waiting or heavy DSP while RX FIFO is live. 0=continue,
 * 1=end at a complete stereo wire frame, negative errno=abort. No file.
 * Callback chunks are provisional: discard recognition/state if ioctl fails;
 * only a successful return qualifies the complete input transport. */
struct a733_audio_live_s
{
  uint32_t version,max_ms,gain;
  int (*consume)(void *,const uint8_t *,uint32_t bytes);
  void *cookie;
  uint32_t frames,ended;
};
/* FLAT-only synchronous pull source. First callback runs before TX starts;
 * subsequent callbacks MUST NOT block: queued RAM only, no model/file I/O.
 * Return 0/bytes=0 for EOF, negative errno for failure/underrun. Up to 10s.
 * Callback/cookie remain valid until ioctl returns; no asynchronous ownership. */
struct a733_audio_stream_s
{
  uint32_t version;
  uint32_t volume;
  int (*read)(void *,uint8_t *,uint32_t capacity,uint32_t *bytes);
  void *cookie;
  uint32_t frames; /* actual frames submitted, even on a later failure */
};

struct a733_audio_capture_s
{
  uint32_t version;
  uint32_t duration_ms;           /* 100..10000 */
  uint32_t gain;                  /* 1..16, recommended 5 */
  uint32_t capacity;
  uint8_t *pcm;                   /* mono S16LE, 48000 Hz */
  uint32_t bytes;                 /* zero unless entire capture succeeds */
};

struct a733_audio_play_s
{
  uint32_t version;
  uint32_t rate;                  /* 8000/16000/24000/48000 */
  uint32_t channels;              /* 1/2, S16LE */
  uint32_t bytes;                 /* whole frames, <=10 seconds */
  const uint8_t *pcm;
  uint32_t volume;                /* linear amplitude 0..100 */
};

struct a733_audio_file_s
{
  uint32_t version;
  uint32_t volume;
  const char *path;               /* /data/ regular PCM16 WAV, <=10 seconds */
};
#endif
