/* SPDX-License-Identifier: Apache-2.0 */
#include "voice_fbank.hxx"
#include "third_party/kaldi-native-fbank/csrc/include/online-feature.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <exception>
#include <new>

namespace aipet {
static knf::FbankOptions options()
{
  knf::FbankOptions o;
  o.frame_opts.samp_freq=16000;
  o.frame_opts.dither=0;
  o.frame_opts.snip_edges=false;
  o.mel_opts.num_bins=80;
  o.mel_opts.high_freq=-400;
  return o;
}
struct VoiceFbank {
  knf::OnlineFbank bank{options()};
  unsigned samples=0;
  bool finished=false;
  int error=0;
};

int voice_fbank_create(VoiceFbank **out)
{
  if (!out) return -EINVAL;
  *out=nullptr;
#if defined(__cpp_exceptions)
  try {
#endif
    *out=new(std::nothrow) VoiceFbank;
    return *out ? 0 : -ENOMEM;
#if defined(__cpp_exceptions)
  } catch (const std::bad_alloc &) { return -ENOMEM; }
    catch (const std::exception &) { return -EIO; }
#endif
}
void voice_fbank_destroy(VoiceFbank *s) { delete s; }

int voice_fbank_accept(VoiceFbank *s,const unsigned char *pcm,std::size_t bytes)
{
  if (!s || (bytes && !pcm) || bytes%2) return -EINVAL;
  if (s->error) return s->error;
  if (s->finished) return -EPERM;
  if (bytes/2>voice_fbank_max_samples-s->samples) return -E2BIG;
#if defined(__cpp_exceptions)
  try {
#endif
    /* Bound transient waveform storage even when the caller supplies 10s. */
    float chunk[320];
    while (bytes) {
      unsigned n=static_cast<unsigned>(std::min<std::size_t>(bytes/2,320));
      for (unsigned i=0;i<n;i++) {
        unsigned u=unsigned(pcm[2*i]) | (unsigned(pcm[2*i+1])<<8);
        int value=u>=32768 ? static_cast<int>(u)-65536 : static_cast<int>(u);
        chunk[i]=static_cast<float>(value)/32768.0f;
      }
      s->bank.AcceptWaveform(16000,chunk,n);
      s->samples+=n; bytes-=n*2; pcm+=n*2;
    }
    return 0;
#if defined(__cpp_exceptions)
  } catch (const std::bad_alloc &) { return s->error=-ENOMEM; }
    catch (const std::exception &) { return s->error=-EIO; }
#endif
}
int voice_fbank_finish(VoiceFbank *s)
{
  if (!s) return -EINVAL;
  if (s->error) return s->error;
  if (s->finished) return 0;
  if (!s->samples) return -ENODATA;
#if defined(__cpp_exceptions)
  try {
#endif
    s->bank.InputFinished(); s->finished=true;
    return 0;
#if defined(__cpp_exceptions)
  } catch (const std::bad_alloc &) { return s->error=-ENOMEM; }
    catch (const std::exception &) { return s->error=-EIO; }
#endif
}
int voice_fbank_frames(const VoiceFbank *s)
{
  if (!s) return -EINVAL;
  return s->error ? s->error : s->bank.NumFramesReady();
}
int voice_fbank_copy(const VoiceFbank *s,unsigned first,unsigned frames,
                     float *out,std::size_t capacity)
{
  if (!s || (frames && !out)) return -EINVAL;
  if (s->error) return s->error;
  unsigned ready=static_cast<unsigned>(s->bank.NumFramesReady());
  if (first>ready || frames>ready-first) return -ERANGE;
  if (frames>capacity/voice_fbank_bins) return -ENOSPC;
  for (unsigned i=0;i<frames;i++)
    std::memcpy(out+i*voice_fbank_bins,s->bank.GetFrame(first+i),
                voice_fbank_bins*sizeof(float));
  return 0;
}
}
