/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <cstddef>
#include <cstdint>
namespace aipet {
struct VoiceAcoustic;
struct VoiceAcousticMel { const float *data=nullptr;unsigned frames=0;void *tensor=nullptr; };
using VoiceAcousticStop=int (*)(); // static-lifetime, nonblocking/thread-safe getter
/* CPU Matcha only, not a vocoder/ASR/board-acceptance claim. One externally
 * serialized owner; one outstanding mel. Model is immutable resident RAM.
 * Use a single ORT compute thread so no worker survives a NuttX task group.
 * A bounded monitor terminates ORT between kernels at the deadline/stop flag.
 * The optional stop getter has no caller cookie and must have static lifetime.
 * This component does not install a target voice provider. */
int voice_acoustic_open(const char *ort_model,VoiceAcoustic **);
int voice_acoustic_run(VoiceAcoustic *,const int64_t *ids,std::size_t count,
                        float noise_scale,float length_scale,VoiceAcousticMel *,
                        long long deadline_us=0,VoiceAcousticStop=nullptr);
void voice_acoustic_mel_release(VoiceAcoustic *,VoiceAcousticMel *);
void voice_acoustic_close(VoiceAcoustic *);
}
