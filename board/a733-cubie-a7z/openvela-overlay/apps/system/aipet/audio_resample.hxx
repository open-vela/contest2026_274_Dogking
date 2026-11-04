/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <cstddef>
namespace aipet {
/* Matcha/HiFi-GAN WAV adapter: PCM16 mono/22050 only, bounded 10 seconds.
 * No rate relabeling, cloud calls, inference, file writes or hardware ownership.
 */
int matcha_wav_to_48k(const unsigned char *wav,std::size_t size,
                      unsigned char *pcm,std::size_t capacity,std::size_t *used);
struct MatchaPcmStream;
using MatchaPcmSink=int (*)(void *,const unsigned char *,std::size_t);
/* Continuous 320/147 phase across all vocoder chunks; 16 input-sample
 * lookahead, 10ms output blocks. Same taps/rounding as the WAV reference.
 * PCM may already be audible on later error; never claim rollback. */
int matcha_pcm_stream_open(MatchaPcmSink,void *,MatchaPcmStream **);
int matcha_pcm_stream_open_limit(MatchaPcmSink,void *,MatchaPcmStream **,unsigned max_samples);
int matcha_pcm_stream_accept(MatchaPcmStream *,const unsigned char *,std::size_t);
int matcha_pcm_stream_finish(MatchaPcmStream *);
void matcha_pcm_stream_close(MatchaPcmStream *);
}
