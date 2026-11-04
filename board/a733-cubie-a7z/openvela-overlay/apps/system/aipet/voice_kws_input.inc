/* SPDX-License-Identifier: Apache-2.0 */
/* Included by audio_app.cxx, inside aipet. Explicit buffered diagnostic only:
 * no streaming claim, file writes, ASR/cloud, TTS, Agent or physical actions.
 * Existing operation_lock + AudioInterruptGuard cover the complete operation.
 */
static uint32_t kws_u32(const unsigned char *p)
{ return unsigned(p[0])|(unsigned(p[1])<<8)|(unsigned(p[2])<<16)|(unsigned(p[3])<<24); }
static unsigned kws_u16(const unsigned char *p) { return unsigned(p[0])|(unsigned(p[1])<<8); }
static int kws_wave(const unsigned char *p,size_t bytes,const unsigned char **pcm,
                     size_t *length,unsigned *rate)
{
  if(!pcm || !length || !rate)return -EINVAL;
  *pcm=nullptr;*length=0;*rate=0;
  if(!p || bytes<44 || bytes>1025536 || std::memcmp(p,"RIFF",4) ||
     std::memcmp(p+8,"WAVE",4) || uint64_t(kws_u32(p+4))+8!=bytes)return -EINVAL;
  bool fmt=false,data=false;size_t pos=12;unsigned chunks=0;
  while(pos<bytes) {
    if(++chunks>128 || bytes-pos<8)return -EINVAL;
    const unsigned char *name=p+pos;size_t n=kws_u32(p+pos+4);pos+=8;
    if(n>bytes-pos)return -EINVAL;
    if(!std::memcmp(name,"fmt ",4)) {
      if(fmt || n!=16 || kws_u16(p+pos)!=1 || kws_u16(p+pos+2)!=1 ||
         kws_u16(p+pos+12)!=2 || kws_u16(p+pos+14)!=16)return -EINVAL;
      *rate=kws_u32(p+pos+4);
      if((*rate!=16000 && *rate!=48000) || kws_u32(p+pos+8)!=*rate*2u)return -ENOTSUP;
      fmt=true;
    } else if(!std::memcmp(name,"data",4)) {
      if(data || !n || n%2)return -EINVAL;
      *pcm=p+pos;*length=n;data=true;
    }
    pos+=n;
    if(n&1u) { if(pos==bytes)return -EINVAL; ++pos; }
  }
  if(!fmt || !data || *length<size_t(*rate) || *length>size_t(*rate)*20u ||
     (*rate==48000 && *length%6))return -EINVAL;
  return 0; /* 0.5..10 s, exact S16LE mono; no implicit trim/rate guessing. */
}
struct KwsFeatures {
  std::unique_ptr<float[]> values;
  unsigned frames=0;
  ~KwsFeatures() { if(values)aipet_wipe(values.get(),size_t(frames)*320); }
};
/* One production frontend for inference AND numerical diagnostics. Its KNF
 * waveform/FFT state and decimation buffer are released BEFORE NPU prepare. */
static int kws_extract(const unsigned char *pcm,size_t bytes,unsigned rate,KwsFeatures &out)
{
  if(!pcm || out.values || (rate!=16000 && rate!=48000) || bytes%2 ||
     bytes<size_t(rate) || bytes>size_t(rate)*20 || (rate==48000 && bytes%6))return -EINVAL;
  std::unique_ptr<unsigned char[]> down;
  const unsigned char *input=pcm;size_t used=bytes;int ret=0;
  if(rate==48000) {
    down.reset(new(std::nothrow) unsigned char[bytes/3]);
    ret=down?audio_decimate(pcm,bytes,down.get(),bytes/3,&used):-ENOMEM;
    input=down.get();
  }
  VoiceFbank *raw=nullptr;if(!ret)ret=voice_fbank_create(&raw);
  std::unique_ptr<VoiceFbank,decltype(&voice_fbank_destroy)> bank(raw,voice_fbank_destroy);
  if(!ret)ret=voice_fbank_accept(bank.get(),input,used);
  if(!ret)ret=voice_fbank_finish(bank.get());
  int frames=ret?0:voice_fbank_frames(bank.get());
  if(!ret && frames<0)ret=frames;
  if(!ret && (frames<29 || frames>1000))ret=-ERANGE;
  if(!ret) {
    out.frames=static_cast<unsigned>(frames);
    out.values.reset(new(std::nothrow) float[out.frames*80]);
    if(!out.values)ret=-ENOMEM;
  }
  if(!ret)ret=voice_fbank_copy(bank.get(),0,out.frames,out.values.get(),out.frames*80);
  for(unsigned i=0;!ret && i<out.frames*80;++i)
    if(!std::isfinite(out.values[i]))ret=-EDOM;
  if(down)aipet_wipe(down.get(),bytes/3);
  return ret;
}
static int kws_pcm(const char *base,const unsigned char *pcm,size_t bytes,unsigned rate)
{
  KwsFeatures features;
  int ret=kws_extract(pcm,bytes,rate,features);
  if(!ret && interrupted())ret=-ECANCELED;
  if(!ret) {
    std::printf("npukws-input: buffered frontend PCM=16000/mono/S16 frames=%u bins=80; capture already stopped; NOT live streaming\n",features.frames);
    ret=voice_npu_kws_memory(base,features.values.get(),features.frames);
  }
  return ret;
}
static int kws_wav_file(const char *base,const char *path)
{
  if(!base || !path || std::strncmp(path,"/data/",6))return -EINVAL;
  int fd=open(path,O_RDONLY|O_NONBLOCK|O_NOFOLLOW);if(fd<0)return -errno;
  struct stat st{};int ret=fstat(fd,&st)<0?-errno:0;
  if(!ret && (!S_ISREG(st.st_mode) || st.st_size<44 || st.st_size>1025536))ret=-EINVAL;
  size_t bytes=ret?0:static_cast<size_t>(st.st_size),done=0;
  std::unique_ptr<unsigned char[]> data;
  if(!ret){data.reset(new(std::nothrow) unsigned char[bytes]);if(!data)ret=-ENOMEM;}
  while(!ret && done<bytes) {
    ssize_t n=read(fd,data.get()+done,bytes-done);
    if(n<0 && errno==EINTR)continue;
    if(n<=0)ret=n<0?-errno:-EIO;else done+=static_cast<size_t>(n);
  }
  if(close(fd)<0 && !ret)ret=-errno;
  const unsigned char *pcm=nullptr;size_t length=0;unsigned rate=0;
  if(!ret)ret=kws_wave(data.get(),bytes,&pcm,&length,&rate);
  if(!ret)ret=kws_pcm(base,pcm,length,rate);
  if(data)aipet_wipe(data.get(),bytes);
  return ret;
}
static int kws_mic(const char *base,unsigned ms,unsigned gain)
{
  std::unique_ptr<unsigned char[]> pcm(new(std::nothrow) unsigned char[ms*96u]);
  if(!pcm)return -ENOMEM;
  size_t bytes=0;
  std::printf("npukws-input: recording=%u ms gain=x%u; local RAM only; record THEN infer; no file/upload/actions\n",ms,gain);
  int ret=a733_pcm_capture(ms,gain,pcm.get(),ms*96u,&bytes);
  if(!ret && interrupted())ret=-ECANCELED;
  if(!ret && bytes!=ms*96u)ret=-EIO;
  if(!ret)ret=kws_pcm(base,pcm.get(),bytes,48000);
  aipet_wipe(pcm.get(),ms*96u);return ret;
}
