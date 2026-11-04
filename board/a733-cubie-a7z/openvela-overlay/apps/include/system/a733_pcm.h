/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Public product adapter. Negative errno on failure; synchronous <=10 s PCM
 * transfers, EBUSY if hardware is owned. No automatic cloud or disk access. */
int a733_pcm_available(void);
int a733_pcm_capture(unsigned ms, unsigned gain, unsigned char *pcm,
                     size_t capacity, size_t *bytes);
int a733_pcm_play(const unsigned char *pcm, size_t bytes, unsigned rate,
                  unsigned channels, unsigned volume);
int a733_pcm_play_file(const char *path, unsigned volume);
typedef int (*a733_pcm_reader)(void *,unsigned char *,uint32_t,uint32_t *);
int a733_pcm_stream(a733_pcm_reader,void *,unsigned volume,uint32_t *frames);
typedef int (*a733_pcm_capture_sink)(void *,const unsigned char *,uint32_t);
int a733_pcm_capture_stream(unsigned max_ms,unsigned gain,a733_pcm_capture_sink,
                            void *,uint32_t *frames,unsigned *ended);
/* Explicit continuous idle RX; callback must check cooperative cancellation. */
int a733_pcm_monitor(unsigned gain,a733_pcm_capture_sink,void *,uint64_t *frames,
                     unsigned *ended);
#ifdef __cplusplus
}
#endif
