/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <cerrno>
#include <cstring>
namespace aipet {
using LiveSink = int (*)(void *, const unsigned char *, std::size_t);
struct DecimateLive;
int decimate_live_open(LiveSink, void *, DecimateLive **);
int decimate_live_accept(DecimateLive *, const unsigned char *, std::size_t);
int decimate_live_finish(DecimateLive *);
void decimate_live_close(DecimateLive *);
struct LiveCaptureMetrics {
  unsigned samples48 = 0, samples16 = 0, queue_peak = 0;
  bool ended = false, speech = false;
  long long first_pcm16_us = 0, elapsed_us = 0;
  long long capture_stopped_us = 0, consumer_finished_us = 0; // absolute monotonic
  bool consumer_retained = false;
  bool limited = false;
  uint64_t monitor_frames = 0;
};
/* CPU energy gate, NOT a trained VAD/AEC. 400ms retained pre-roll, 400ms
 * initial quiet calibration, 160ms attack and 700ms hangover.
 * No heap/IO/model calls in push(). All emitted audio includes the prefix;
 * no speech means indefinite idle, not a consumed conversational turn. */
struct DialogueGate {
  unsigned char ring[40][960]{};
  uint64_t seen=0,noise=UINT64_MAX,last_energy=0;
  unsigned last_peak=0;
  unsigned attack=0,voiced=0,quiet=0,emitted=0;
  unsigned continuation_run=0,continuation_peak=0,endpoint_resets=0,rejected_bursts=0,quiet_peak=0;
  bool ready=false,speech=false,ended=false,limited=false;
  unsigned floor=400;
  int push(const unsigned char *pcm,std::size_t bytes,LiveSink sink,void *cookie) {
    if(!pcm || bytes!=960 || !sink || ended)return -EINVAL;
    int64_t sum=0;uint64_t square=0;last_peak=0;
    for(unsigned i=0;i<480;i++) {
      int v=int16_t(unsigned(pcm[2*i])|(unsigned(pcm[2*i+1])<<8));
      sum+=v;square+=int64_t(v)*v;
      unsigned magnitude=v<0?unsigned(-v):unsigned(v);
      if(magnitude>last_peak)last_peak=magnitude;
    }
    const uint64_t energy=square-uint64_t(sum*sum/480);
    last_energy=energy;
    if(!speech) {
      std::memcpy(ring[seen%40],pcm,960);++seen;
      if(!ready) {
        if(energy<noise)noise=energy;
        if(seen>=40)ready=true;
        return 0;
      }
      const uint64_t base=uint64_t(floor)*floor*480;
      const uint64_t high=noise>UINT64_MAX/9?UINT64_MAX:noise*9;
      bool active=energy>(high>base?high:base);
      if(active)++attack;
      else {
        attack=0;
        /* Do not learn sub-trigger speech as ever-rising background. Quiet
         * can lower the estimate quickly; upward tracking is restricted to
         * qualified quiet candidates. Never lower the configured floor. */
        if(energy<noise)noise=(noise+energy)/2;
        else if(energy<=((noise*2>base/4)?noise*2:base/4))
          noise=(31*noise+energy)/32;
      }
      if(attack<16)return 0;
      speech=true;voiced=attack;
      const unsigned count=seen<40?unsigned(seen):40;
      for(unsigned i=0;i<count;i++) {
        int rc=sink(cookie,ring[(seen-count+i)%40],960);if(rc)return rc;
        ++emitted;
      }
      return 0;
    }
    ++seen;
    const uint64_t base=uint64_t(floor)*floor*480;
    // One above-threshold 10ms spike is not a new speech segment. v223 still
    // reset quiet on every such spike, so intermittent room noise defeated
    // the 700ms endpoint despite a healthy ASR. Confirm 80ms continuous
    // activity before resetting accumulated quiet; pending active blocks are
    // NOT counted as silence and every PCM block is still sent unchanged.
    // 80ms protects short resumed syllables; it is NOT trained VAD/AEC.
    const uint64_t continuation=noise>UINT64_MAX/9?UINT64_MAX:noise*9;
    if(energy>(continuation>base?continuation:base)) {
      ++voiced;
      if(continuation_run<8)++continuation_run;
      if(continuation_run>continuation_peak)continuation_peak=continuation_run;
      if(continuation_run==8){if(quiet)++endpoint_resets;quiet=0;}
    } else {
      if(continuation_run && continuation_run<8)++rejected_bursts;
      continuation_run=0;++quiet;if(quiet>quiet_peak)quiet_peak=quiet;
    }
    int rc=sink(cookie,pcm,960);if(rc)return rc;++emitted;
    if(voiced>=16 && quiet>=70)ended=true;
    if(emitted>=800){limited=true;ended=true;}
    return ended?1:0;
  }
};
using LiveVoiceState = void (*)(void *,unsigned); // 1=ready, 2=speech, 3=RX stopped
int audio_capture_dialogue(unsigned gain,unsigned floor,LiveSink pcm16,void *,
    int (*cancel)(void *),void *,LiveVoiceState,void *,LiveCaptureMetrics *,uint64_t *noise_profile=nullptr);
/* Consumer callbacks run on a separate thread and MUST bound their own engine
 * calls; the cooperative deadline cannot interrupt an unbounded callback.
 * RX callback copies RAM and computes bounded integer energy over raw 48k
 * 10ms windows; endpoint detection never waits for the NPU consumer. This
 * raw-PCM threshold is not the old post-FIR energy calibration or trained VAD.
 * PCM is provisional until the ENTIRE run succeeds, including driver proof,
 * consumer completion and FIR tail. Never act on provisional recognition.
 * No hardware reset, file, cloud or automatic model/provider installation.
 * endpoint_rms=0 disables diagnostic energy endpointing; otherwise 100..5000.
 * This energy detector is NOT a qualified ASR/VAD model or actuator intent. */
/* -EOWNERDEAD + consumer_retained means join failed. The queue AND every
 * callback/cancel cookie must remain alive until reboot. Do not resume audio.
 * Internal callers use heap owners/static cancellation and retain them. */
int audio_capture_live(unsigned ms, unsigned gain, unsigned endpoint_rms,
                       LiveSink pcm48, LiveSink pcm16, void *cookie,
                       int (*cancel)(void *), void *cancel_cookie,
                       LiveCaptureMetrics *);
} // namespace aipet
