/* SPDX-License-Identifier: Apache-2.0 */
#ifndef AIPET_LOCAL_ASR_H
#define AIPET_LOCAL_ASR_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define LOCAL_ASR_FRAME_SAMPLES 320 /* 20 ms at 16 kHz */
/* Owner thread serializes feed/finish/abort/destroy. Engines must copy sample
 * data before returning. Results are UTF-8; partial text is not a command. */
struct local_asr_engine
{
  int (*open)(void *config, void **session);
  int (*accept)(void *session, const int16_t *samples, size_t count);
  int (*result)(void *session, char *text, size_t capacity, int final);
  void (*close)(void *session);
};
enum local_asr_state { LOCAL_ASR_ACTIVE, LOCAL_ASR_FINISHED,
                       LOCAL_ASR_FAILED, LOCAL_ASR_ABORTED };
struct local_asr_status
{
  enum local_asr_state state;
  uint64_t bytes_received;
  uint64_t samples_submitted;
  unsigned frames;
  int last_error;
};
struct local_asr_stream;
int local_asr_open(const struct local_asr_engine *, void *config,
                   struct local_asr_stream **stream);
/* s16le/16kHz/mono only; odd byte chunk boundaries are supported. */
int local_asr_feed(struct local_asr_stream *, const void *pcm, size_t bytes);
int local_asr_partial(struct local_asr_stream *, char *, size_t capacity);
int local_asr_finish(struct local_asr_stream *, char *, size_t capacity);
void local_asr_abort(struct local_asr_stream *);
void local_asr_destroy(struct local_asr_stream *);
int local_asr_get_status(const struct local_asr_stream *, struct local_asr_status *);
/* Optional official Media adapter, supplied by openvela_capture.c. */
int local_asr_capture_media(struct local_asr_stream *, unsigned milliseconds);
#ifdef __cplusplus
}
#endif
#endif
