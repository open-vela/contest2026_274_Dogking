/* SPDX-License-Identifier: Apache-2.0 */
#include "audio_resample.hxx"
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
namespace aipet {
static unsigned u16(const unsigned char *p) { return p[0]|(unsigned(p[1])<<8); }
static uint32_t u32(const unsigned char *p) { return u16(p)|(uint32_t(u16(p+2))<<16); }
int matcha_wav_to_48k(const unsigned char *wav,std::size_t size,
                      unsigned char *out,std::size_t cap,std::size_t *used)
{
  if (!used) return -EINVAL;
  *used=0;
  if (!wav || !out || size<44 || size>1048576 || std::memcmp(wav,"RIFF",4) ||
      std::memcmp(wav+8,"WAVE",4) || uint64_t(u32(wav+4))+8!=size) return -EINVAL;
  bool format=false;
  const unsigned char *data=nullptr;
  std::size_t bytes=0;
  for (std::size_t offset=12;offset<size;) {
    if (size-offset<8) return -EINVAL;
    uint32_t length=u32(wav+offset+4);
    const auto *chunk=wav+offset;
    offset+=8;
    if (length>size-offset || (length&1u)>size-offset-length) return -EINVAL;
    if (!std::memcmp(chunk,"fmt ",4)) {
      if (format || length<16 || u16(wav+offset)!=1 || u16(wav+offset+2)!=1 ||
          u32(wav+offset+4)!=22050 || u32(wav+offset+8)!=44100 ||
          u16(wav+offset+12)!=2 || u16(wav+offset+14)!=16) return -ENOTSUP;
      format=true;
    } else if (!std::memcmp(chunk,"data",4)) {
      if (data || !length || (length&1) || length>441000) return -EINVAL;
      data=wav+offset; bytes=length;
    }
    offset+=length+(length&1u);
  }
  if (!format || !data) return -EINVAL;
  const std::size_t samples=bytes/2, frames=(samples*320+146)/147;
  if (frames>480000 || cap<frames*2) return -ENOSPC;
  /* 320/147 rational conversion. 32-tap Hann-windowed sinc, normalized
   * per fractional phase, 0.45 input cycles/sample cutoff. CPU-only DSP.
   * Clamp edges instead of wrapping; do not apply hidden loudness gain.
   */
  constexpr double pi=3.14159265358979323846;
  /* Process each phase's strided output to keep stack below 1 KiB. */
  for (unsigned start=0;start<320;start++) {
    const unsigned phase=(start*147)%320;
    double taps[32];
    double sum=0;
    for (int k=0;k<32;k++) {
      const double x=double(k-15)-double(phase)/320;
      const double sinc=std::fabs(x)<1e-12 ? 0.9 : std::sin(0.9*pi*x)/(pi*x);
      const double window=std::fabs(x)<16 ? 0.5+0.5*std::cos(pi*x/16) : 0;
      taps[k]=sinc*window; sum+=taps[k];
    }
    for (auto &tap:taps) tap/=sum;
    for (std::size_t n=start;n<frames;n+=320) {
      const std::size_t base=n*147/320;
      double value=0;
      for (int k=0;k<32;k++) {
        long index=static_cast<long>(base)+k-15;
        if (index<0) index=0;
        if (static_cast<std::size_t>(index)>=samples) index=samples-1;
        unsigned raw=u16(data+index*2);
        value+=(raw>=32768 ? int(raw)-65536 : int(raw))*taps[k];
      }
      long sample=std::lround(value);
      if (sample>32767) sample=32767;
      if (sample< -32768) sample=-32768;
      uint16_t raw=static_cast<uint16_t>(sample);
      out[n*2]=raw&255; out[n*2+1]=raw>>8;
    }
  }
  *used=frames*2;
  return 0;
}
}
