/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace aipet {
struct VoiceTtsFrontend;
/* Matcha-baker character/phrase lexicon. All inputs are bounded; malformed
 * UTF-8 or unknown non-whitespace text fails the whole request (no skipped
 * instructions). Original upstream model remains separately licensed.
 * Digits are read individually in Chinese; this is NOT a general TN/FST.
 * Cache one immutable object per model; external serialization is required.
 */
int voice_tts_frontend_open(const char *tokens, const char *lexicon, VoiceTtsFrontend **);
int voice_tts_frontend_text(const VoiceTtsFrontend *, const char *utf8,
                           std::vector<std::vector<int64_t>> &sentences);
void voice_tts_frontend_close(VoiceTtsFrontend *);
/* Logical channel-major mel [80,T] -> [80,200]. No 201..224 truncation.
 * The caller trims synthesized samples to kept_frames*256 for the final chunk.
 */
int voice_tts_mel_chunk(const float *mel, unsigned frames, unsigned first,
                        float *out, std::size_t floats, unsigned *kept_frames);
// 16-frame left/right context; returned skip/keep partition all source frames.
int voice_tts_vocoder_window(const float *,unsigned,unsigned,float *,std::size_t,
                             unsigned *skip,unsigned *keep);
int voice_tts_pcm16(const float *wave, std::size_t samples, unsigned char *out,
                    std::size_t bytes);
}
