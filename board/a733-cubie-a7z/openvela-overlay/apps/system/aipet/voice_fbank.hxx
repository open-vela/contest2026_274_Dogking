/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <cstddef>
namespace aipet {
/* CPU-only Zipformer/KWS frontend. S16LE mono 16kHz, normalized /32768;
 * 80 bins, 25ms Povey/10ms shift, dither=0, snip_edges=false, high=7600Hz.
 * One caller owns each object; max 10s total (1000 frames). No NPU/network.
 * Create/finish/accept may allocate. On no-exceptions RTOS builds the bundled
 * C++ allocator can abort on OOM; bounded input is not an OOM recovery claim.
 */
struct VoiceFbank;
constexpr unsigned voice_fbank_bins=80;
constexpr unsigned voice_fbank_max_samples=160000;
int voice_fbank_create(VoiceFbank **out);
void voice_fbank_destroy(VoiceFbank *state);
int voice_fbank_accept(VoiceFbank *state,const unsigned char *pcm,std::size_t bytes);
int voice_fbank_finish(VoiceFbank *state);
int voice_fbank_frames(const VoiceFbank *state); // count, or negative errno
int voice_fbank_copy(const VoiceFbank *state,unsigned first,unsigned frames,
                     float *out,std::size_t capacity_floats);
}
