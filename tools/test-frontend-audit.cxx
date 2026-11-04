/* SPDX-License-Identifier: Apache-2.0 */
/* Real WAV parser, production frontend, comparison, and exclusive export.
 * Host-only filesystem translation/fault injection, no microphone or NPU. */
#include "../board/a733-cubie-a7z/openvela-overlay/apps/system/aipet/voice_fbank.hxx"
#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <fcntl.h>
#include <fstream>
#include <memory>
#include <new>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
static std::string folder;
static bool cancelled,write_error,write_zero,write_short,write_eintr,sync_error,close_error,cancel_write;
static unsigned submitted,recorded;
extern "C" int __real_open(const char *,int,...);
extern "C" int __real_unlink(const char *);
extern "C" ssize_t __real_write(int,const void *,size_t);
extern "C" int __real_fsync(int);
extern "C" int __real_close(int);
static std::string mapped(const char *p)
{ return !std::strncmp(p,"/data/",6)?folder+"/"+(p+6):p; }
extern "C" int __wrap_open(const char *p,int flags,...)
{
  int mode=0;
  if(flags&O_CREAT){va_list args;va_start(args,flags);mode=va_arg(args,int);va_end(args);}
  return __real_open(mapped(p).c_str(),flags,mode);
}
extern "C" int __wrap_unlink(const char *p){return __real_unlink(mapped(p).c_str());}
extern "C" ssize_t __wrap_write(int fd,const void *p,size_t n)
{
  if(write_error){errno=EIO;return -1;}
  if(write_zero)return 0;
  if(write_eintr){write_eintr=false;errno=EINTR;return -1;}
  if(write_short && n>7)n=7;
  ssize_t result=__real_write(fd,p,n);
  if(cancel_write)cancelled=true;
  return result;
}
extern "C" int __wrap_fsync(int fd)
{if(sync_error){errno=EIO;return -1;}return __real_fsync(fd);}
extern "C" int __wrap_close(int fd)
{int rc=__real_close(fd);if(close_error){errno=EIO;return -1;}return rc;}
static bool interrupted(){return cancelled;}
extern "C" void aipet_wipe(void *p,size_t n){std::memset(p,0,n);}
extern "C" int a733_pcm_capture(unsigned,unsigned,unsigned char *,size_t,size_t *)
{++recorded;return -ENODEV;}
namespace aipet {
static int audio_decimate(const unsigned char *,size_t,unsigned char *,size_t,size_t *)
{return -ENOTSUP;}
int voice_npu_kws_memory(const char *,const float *,unsigned){++submitted;return -ENODEV;}
#include "../board/a733-cubie-a7z/openvela-overlay/apps/system/aipet/voice_kws_input.inc"
#include "../board/a733-cubie-a7z/openvela-overlay/apps/system/aipet/voice_frontend_audit.inc"
}
static std::vector<char> read_file(const std::string &p)
{
  std::ifstream f(p,std::ios::binary|std::ios::ate);assert(f);auto n=f.tellg();assert(n>=0);
  std::vector<char> out(n);f.seekg(0);f.read(out.data(),n);assert(f);return out;
}
static void save(const char *leaf,const void *p,size_t n)
{std::ofstream f(folder+"/"+leaf,std::ios::binary);assert(f);f.write(static_cast<const char *>(p),n);assert(f);}
static bool exists(const char *leaf){return access((folder+"/"+leaf).c_str(),F_OK)==0;}
int main(int argc,char **argv)
{
  assert(argc==4);folder=argv[3];(void)&aipet::kws_wav_file;(void)&aipet::kws_mic;
  for(unsigned i=0;i<6;++i) {
    auto wav=read_file(std::string(argv[1])+"/wave"+std::to_string(i)+".wav");
    auto ref=read_file(std::string(argv[2])+"/features"+std::to_string(i)+".f32");
    save("input.wav",wav.data(),wav.size());save("ref.f32",ref.data(),ref.size());
    assert(!aipet::frontend_audit("wavinfo","/data/input.wav",nullptr));
    assert(!aipet::frontend_audit("fbankcheck","/data/input.wav","/data/ref.f32"));
    const std::string output="/data/export"+std::to_string(i)+".f32";
    assert(!aipet::frontend_audit("fbankexport","/data/input.wav",output.c_str()));
    auto actual=read_file(mapped(output.c_str()));assert(actual==ref);
    /* All same-length mismatches still fail; no approximate golden pass. */
    float changed;std::memcpy(&changed,ref.data()+28,4);changed+=.01f;
    std::memcpy(ref.data()+28,&changed,4);save("ref.f32",ref.data(),ref.size());
    assert(aipet::frontend_audit("fbankcheck","/data/input.wav","/data/ref.f32")==-EILSEQ);
    changed=std::nanf("");std::memcpy(ref.data(),&changed,4);save("ref.f32",ref.data(),ref.size());
    assert(aipet::frontend_audit("fbankcheck","/data/input.wav","/data/ref.f32")==-EDOM);
    save("ref.f32",ref.data(),ref.size()-4);
    assert(aipet::frontend_audit("fbankcheck","/data/input.wav","/data/ref.f32")==-EINVAL);
    assert(aipet::frontend_audit("fbankexport","/data/input.wav",output.c_str())==-EEXIST);
    assert(read_file(mapped(output.c_str()))==actual);
  }
  unsigned char pcm[]={0,0,0xff,0x7f,0xff,0x7f,0,0x80,0,0,0,0x80,1,0};
  auto level=aipet::frontend_level(pcm,sizeof(pcm));
  assert(level.low==-32768 && level.high==32767 && level.rails==4 && level.longest==3 && level.zero==2);
  assert(std::isfinite(level.rms) && level.rms>24000 && level.peak_db==0);
  unsigned char silence[16000]={0};level=aipet::frontend_level(silence,sizeof(silence));
  assert(level.rms==0 && level.dc==0 && level.rails==0 && level.peak_db==-200 && level.rms_db==-200);
  assert(aipet::frontend_crc("123456789",9)==0x2dfd2d88u);
  {
    std::vector<unsigned char> maximum(320000);
    aipet::KwsFeatures long_features;
    assert(!aipet::kws_extract(maximum.data(),maximum.size(),16000,long_features));
    assert(long_features.frames==1000);
    assert(!aipet::frontend_export("/data/maximum.f32",long_features));
    assert(read_file(folder+"/maximum.f32").size()==320000);
    aipet::KwsFeatures invalid;
    assert(aipet::kws_extract(maximum.data(),maximum.size()+2,16000,invalid)==-EINVAL);
    assert(aipet::kws_extract(maximum.data(),100,16000,invalid)==-EINVAL);
    assert(aipet::kws_extract(maximum.data(),16000,22050,invalid)==-EINVAL);
  }
  for(const char *p:{"x","/data/","/data/../escape.f32","/data/a/../../escape.f32","/data//x.f32","/data/./x.f32","/data/a\\b.f32","/data/a\nf32"})
    assert(!aipet::frontend_path(p));
  assert(aipet::frontend_path("/data/npu/voice/kws/features0.f32"));
  assert(!aipet::frontend_path("/data/npu/new.f32",true));
  assert(!mkfifo((folder+"/fifo.wav").c_str(),0600));
  assert(aipet::frontend_audit("wavinfo","/data/fifo.wav",nullptr)==-EINVAL);
  assert(aipet::kws_wav_file("mock","/data/fifo.wav")==-EINVAL);
  assert(!mkdir((folder+"/directory.wav").c_str(),0700));
  assert(aipet::frontend_audit("wavinfo","/data/directory.wav",nullptr)==-EINVAL);
  aipet::FrontendFile missing;
  const char *phase=nullptr;
  assert(aipet::frontend_read("/data/missing-parent/input.wav",44,1024,missing,&phase)==-ENOENT);
  assert(phase && !std::strcmp(phase,"input-open"));
  assert(aipet::frontend_read("/data/input.wav/child.wav",44,1024,missing,&phase)==-ENOTDIR);
  assert(phase && !std::strcmp(phase,"input-open"));
  aipet::KwsFeatures features;
  assert(!aipet::kws_extract(silence,sizeof(silence),16000,features));
  for(const char *p:{"/data/x.wav","/data/new/path.f32","relative.f32"})assert(aipet::frontend_export(p,features)==-EINVAL);
  assert(!symlink("export0.f32",(folder+"/link.f32").c_str()));
  assert(aipet::frontend_export("/data/link.f32",features)==-EEXIST);
  assert(aipet::frontend_audit("fbankcheck","/data/input.wav","/data/link.f32")<0);
  assert(!aipet::frontend_export("/data/ok.f32",features));
  auto golden=read_file(folder+"/ok.f32");
  write_short=true;write_eintr=true;
  assert(!aipet::frontend_export("/data/short.f32",features));
  assert(read_file(folder+"/short.f32")==golden);write_short=false;
  for(bool *flag:{&write_error,&write_zero,&sync_error,&close_error,&cancel_write}) {
    *flag=true;int rc=aipet::frontend_export("/data/failed.f32",features);*flag=false;cancelled=false;
    assert(rc<0 && !exists("failed.f32"));
  }
  cancelled=true;
  assert(aipet::frontend_audit("fbankexport","/data/input.wav","/data/cancel.f32")==-ECANCELED);
  assert(!exists("cancel.f32"));cancelled=false;
  assert(!aipet::frontend_audit("wavinfo","/data/input.wav",nullptr));
  assert(submitted==0 && recorded==0);
  std::puts("PASS: six actual frontend exports byte-exact; strict deltas/NaN/size fail; level/rails/CRC; containment/final symlink/existing output; short/EINTR/write/fsync/close/cancel cleanup; NO mic/NPU");
}
