/* SPDX-License-Identifier: Apache-2.0 */
/* CPU-only, explicit file diagnostics. Included after voice_kws_input.inc.
 * Never capture, submit NPU work, change gain/thresholds, or contact services.
 */
static bool frontend_path(const char *path,bool leaf=false)
{
  if(!path || std::strncmp(path,"/data/",6))return false;
  size_t n=strnlen(path,256);
  if(n<=6 || n==256 || path[n-1]=='/')return false;
  const char *start=path+6;
  for(const char *p=start;;++p) {
    unsigned char ch=static_cast<unsigned char>(*p);
    if(ch && (ch<32 || ch==127 || ch=='\\'))return false;
    if(ch=='/' || !ch) {
      size_t len=static_cast<size_t>(p-start);
      if(!len || (len==1 && *start=='.') || (len==2 && start[0]=='.' && start[1]=='.'))return false;
      if(ch && leaf)return false;
      if(!ch)break;
      start=p+1;
    }
  }
  return true;
}
struct FrontendFile {
  std::unique_ptr<unsigned char[]> data;
  size_t bytes=0;
  ~FrontendFile() { if(data)aipet_wipe(data.get(),bytes); }
};
static int frontend_read(const char *path,size_t min,size_t max,FrontendFile &out,
                         const char **phase=nullptr)
{
  if(phase)*phase="input-path";
  if(!frontend_path(path) || out.data)return -EINVAL;
  /* Nonblocking open ensures a FIFO is rejected by fstat rather than hanging
   * before the regular-file gate. Regular filesystem files are unaffected. */
  if(phase)*phase="input-open";
  int fd=open(path,O_RDONLY|O_NONBLOCK|O_NOFOLLOW);if(fd<0)return -errno;
  if(phase)*phase="input-stat";
  struct stat st{};int ret=fstat(fd,&st)<0?-errno:0;
  if(!ret && (!S_ISREG(st.st_mode) || st.st_size<static_cast<off_t>(min) ||
              st.st_size>static_cast<off_t>(max)))ret=-EINVAL;
  if(!ret) {
    out.bytes=static_cast<size_t>(st.st_size);
    out.data.reset(new(std::nothrow) unsigned char[out.bytes]);
    if(!out.data)ret=-ENOMEM;
  }
  size_t done=0;
  if(!ret && phase)*phase="input-read";
  while(!ret && done<out.bytes) {
    if(interrupted()){ret=-ECANCELED;break;}
    ssize_t n=read(fd,out.data.get()+done,out.bytes-done);
    if(n<0 && errno==EINTR)continue;
    if(n<=0)ret=n<0?-errno:-EIO;else done+=static_cast<size_t>(n);
  }
  /* A growing/shrinking file is not a valid snapshot. Same-size concurrent
   * mutations cannot be excluded: the printed content CRC is authoritative. */
  unsigned char extra=0;
  if(!ret) {
    ssize_t n;
    do {n=read(fd,&extra,1);}while(n<0 && errno==EINTR && !interrupted());
    if(n!=0)ret=n<0?-errno:-ESTALE;
  }
  if(close(fd)<0 && !ret){ret=-errno;if(phase)*phase="input-close";}
  if(!ret && phase)*phase="input-ready";
  return ret;
}
/* Same raw reflected CRC convention as the tensor client: seed=0, no final
 * complement. Slow bitwise implementation is diagnostic-only, not hot NPU. */
static uint32_t frontend_crc(const void *data,size_t bytes)
{
  const auto *p=static_cast<const unsigned char *>(data);uint32_t crc=0;
  while(bytes--) {
    crc^=*p++;
    for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);
  }
  return crc;
}
struct FrontendLevel {
  int low=32767,high=-32768;
  unsigned rails=0,longest=0,zero=0;
  double rms=0,dc=0,peak_db=-200,rms_db=-200;
};
static FrontendLevel frontend_level(const unsigned char *pcm,size_t bytes)
{
  FrontendLevel s;unsigned run=0;double squares=0,sum=0;
  for(size_t i=0;i<bytes;i+=2) {
    unsigned u=kws_u16(pcm+i);int v=u>=32768?static_cast<int>(u)-65536:static_cast<int>(u);
    if(v<s.low)s.low=v;
    if(v>s.high)s.high=v;
    if(!v)++s.zero;
    if(v==32767 || v==-32768){++s.rails;++run;}else run=0;
    if(run>s.longest)s.longest=run;
    sum+=v;squares+=double(v)*v;
  }
  s.dc=sum/(bytes/2);s.rms=std::sqrt(squares/(bytes/2));
  double peak=std::fmax(-double(s.low),double(s.high));
  if(peak>0)s.peak_db=20*std::log10(peak/32768);
  if(s.rms>0)s.rms_db=20*std::log10(s.rms/32768);
  return s;
}
static void frontend_print_level(const unsigned char *pcm,size_t bytes,unsigned rate)
{
  FrontendLevel s=frontend_level(pcm,bytes);
  std::printf("wavinfo: rate=%u channels=1 bits=16 samples=%zu duration-ms=%zu pcm-crc=%08lx\n",
    rate,bytes/2,(bytes/2)*1000/rate,static_cast<unsigned long>(frontend_crc(pcm,bytes)));
  std::printf("wavinfo: min=%d max=%d rms=%.9g dc=%.9g peak-dBFS=%.6g rms-dBFS=%.6g rails=%u rail-run=%u zeros=%u\n",
    s.low,s.high,s.rms,s.dc,s.peak_db,s.rms_db,s.rails,s.longest,s.zero);
  std::puts("wavinfo: saved PCM only; rails indicate saturation risk, not its cause; no gain/AGC changes");
}
struct FrontendDifference {
  size_t differing=0,first=0;
  double maximum=0,rmse=0;
};
static int frontend_difference(const float *actual,const unsigned char *expected,
                               size_t count,FrontendDifference &out)
{
  if(!actual || !expected || !count || count>80000)return -EINVAL;
  double squares=0;out={};
  for(size_t i=0;i<count;++i) {
    float ref;std::memcpy(&ref,expected+i*4,4);
    if(!std::isfinite(ref) || std::fabs(ref)>1000 || !std::isfinite(actual[i]))return -EDOM;
    if(std::memcmp(actual+i,expected+i*4,4)) {
      if(!out.differing)out.first=i;
      ++out.differing;
    }
    double error=std::fabs(double(actual[i])-double(ref));
    if(error>out.maximum)out.maximum=error;
    squares+=error*error;
  }
  out.rmse=std::sqrt(squares/count);return 0;
}
static int frontend_export(const char *path,const KwsFeatures &f)
{
  /* Output is a NEW direct child of /data (no parent symlink traversal).
   * O_EXCL protects any existing file, including a symlink. No rename that
   * could overwrite somebody else's destination. A power cut may leave a
   * partial file: fbankcheck's exact length gate rejects it; use a NEW name. */
  if(!frontend_path(path,true) || !f.values || !f.frames)return -EINVAL;
  size_t n=std::strlen(path);
  if(n<11 || std::strcmp(path+n-4,".f32"))return -EINVAL;
  if(interrupted())return -ECANCELED;
  int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);if(fd<0)return -errno;
  const auto *p=reinterpret_cast<const unsigned char *>(f.values.get());
  size_t bytes=size_t(f.frames)*320,done=0;int ret=0;
  while(!ret && done<bytes) {
    if(interrupted()){ret=-ECANCELED;break;}
    ssize_t wrote=write(fd,p+done,bytes-done);
    if(wrote<0 && errno==EINTR)continue;
    if(wrote<=0)ret=wrote<0?-errno:-EIO;else done+=static_cast<size_t>(wrote);
  }
  if(!ret && fsync(fd)<0)ret=-errno;
  if(close(fd)<0 && !ret)ret=-errno;
  if(!ret && interrupted())ret=-ECANCELED;
  if(ret) {
    if(unlink(path)<0)std::printf("fbankexport: cleanup failed=%d; partial output may remain\n",errno);
  } else std::printf("fbankexport: saved=%s bytes=%zu; local diagnostic features, not an accuracy certificate\n",path,bytes);
  return ret;
}
static int frontend_audit(const char *command,const char *wav,const char *other)
{
  static_assert(sizeof(float)==4,"F32 diagnostic ABI");
  const uint32_t one=1;
  if(*reinterpret_cast<const unsigned char *>(&one)!=1)return -ENOTSUP;
  bool info=!std::strcmp(command,"wavinfo"),check=!std::strcmp(command,"fbankcheck");
  if(!info && !check && std::strcmp(command,"fbankexport"))return -EINVAL;
  if(!info && (!other || (check?!frontend_path(other):!frontend_path(other,true))))return -EINVAL;
  FrontendFile source;int ret=frontend_read(wav,44,1025536,source);
  const unsigned char *pcm=nullptr;size_t bytes=0;unsigned rate=0;
  if(!ret)ret=kws_wave(source.data.get(),source.bytes,&pcm,&bytes,&rate);
  if(!ret)frontend_print_level(pcm,bytes,rate);
  if(ret || info)return ret;
  KwsFeatures features;ret=kws_extract(pcm,bytes,rate,features);
  if(!ret && interrupted())ret=-ECANCELED;
  if(ret)return ret;
  size_t size=size_t(features.frames)*320;
  std::printf("frontend-audit: v163 rate=16000 norm=S16/32768 frames=%u bins=80 feature-crc=%08lx compiler=%s\n",
    features.frames,static_cast<unsigned long>(frontend_crc(features.values.get(),size)),__VERSION__);
  std::puts("frontend-audit: production frontend, unchanged math flags; CPU/file only; no mic/NPU/cloud/Agent/playback");
  if(!check)return frontend_export(other,features);
  FrontendFile reference;ret=frontend_read(other,size,size,reference);
  FrontendDifference d;
  if(!ret)ret=frontend_difference(features.values.get(),reference.data.get(),size/4,d);
  if(ret)return ret;
  std::printf("fbankcheck: exact=%u differing=%zu/%zu first-frame=%zu first-bin=%zu maxabs=%.12g rmse=%.12g reference-crc=%08lx\n",
    d.differing?0u:1u,d.differing,size/4,d.first/80,d.first%80,d.maximum,d.rmse,
    static_cast<unsigned long>(frontend_crc(reference.data.get(),size)));
  /* Never promote a tolerance-only result to the strict NPU golden oracle. */
  return d.differing?-EILSEQ:0;
}
