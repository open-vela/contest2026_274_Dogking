/* SPDX-License-Identifier: Apache-2.0 */
/* Explicit prepared-model acceptance entry. No automatic boot inference,
 * capture, speech, cloud request, reset, fallback, or ASR success claim.
 */
#include "voice_npu_check.hxx"
#include "voice_kws.h"
#include "voice_general_contract.hxx"
#include <system/a733_tensor_client.h>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <new>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace aipet {
namespace {
struct Work {
  a733_tensor_client_contract contract{};
  a733_tensor_client *client=nullptr;
  a733_tensor_client_input input[64]{};
  a733_tensor_client_output output[64]{};
  ~Work() {
    if (client) a733_tensor_client_close(client);
    for (unsigned i=0;i<64;++i) {
      std::free(const_cast<void *>(input[i].data));std::free(output[i].data);
    }
  }
};
static const uint32_t encoder_sizes[]={
  9280,32768,24576,12288,12288,3584,3584,16384,12288,6144,6144,3584,3584,
  8192,6144,3072,3072,3584,3584,8192,3072,3072,3072,3584,3584,8192,6144,
  3072,3072,3584,3584,16384,12288,6144,6144,3584,3584,29184,4};
static int contract(const char *name,a733_tensor_client_contract &out)
{
  if (!std::strcmp(name,"encoder")) {
    out.inputs=out.outputs=39;
    std::memcpy(out.input_bytes,encoder_sizes,sizeof(encoder_sizes));
    std::memcpy(out.output_bytes,encoder_sizes,sizeof(encoder_sizes));out.output_bytes[0]=5120;
  } else if (!std::strcmp(name,"decoder")) {
    out.inputs=out.outputs=1;out.input_bytes[0]=8;out.output_bytes[0]=1280;
  } else if (!std::strcmp(name,"joiner")) {
    out.inputs=2;out.outputs=1;out.input_bytes[0]=out.input_bytes[1]=1280;out.output_bytes[0]=1052;
  } else if (!std::strcmp(name,"vocoder")) {
    out.inputs=out.outputs=1;out.input_bytes[0]=64000;out.output_bytes[0]=204800;
  } else return voice_general_contract(name,out);
  return 0;
}
static int read_input(const char *dir,unsigned index,void *destination,std::size_t bytes)
{
  char path[256];int n=std::snprintf(path,sizeof(path),"%s/input%u.bin",dir,index);
  if (n<0 || static_cast<std::size_t>(n)>=sizeof(path)) return -ENAMETOOLONG;
  int fd=open(path,O_RDONLY|O_NOFOLLOW);if (fd<0) return -errno;
  struct stat st;int result=0;
  if (fstat(fd,&st)<0) result=-errno;
  else if (!S_ISREG(st.st_mode) || st.st_size!=static_cast<off_t>(bytes)) result=-EINVAL;
  std::size_t offset=0;
  while (!result && offset<bytes) {
    ssize_t got=read(fd,static_cast<unsigned char *>(destination)+offset,bytes-offset);
    if (got<0 && errno==EINTR) continue;
    if (got<=0) result=got<0?-errno:-EIO;
    else offset+=static_cast<std::size_t>(got);
  }
  if (close(fd)<0 && !result) result=-errno;
  for (std::size_t i=0;!result && i<bytes;i+=4) {
    float value;std::memcpy(&value,static_cast<unsigned char *>(destination)+i,4);
    if (!std::isfinite(value)) result=-EILSEQ;
  }
  return result;
}
static uint32_t crc(const void *memory,std::size_t bytes)
{
  return a733_tensor_client_crc32(memory,bytes,0);
}
}
#include "voice_npu_stream.inc"
int voice_npu_check(int argc,char **argv)
{
  bool dynamic=argc==4 && !std::strcmp(argv[3],"dynamic");
  if ((argc!=3 && !dynamic) || (argc==4 && !dynamic)) {
    std::puts("aipet audio npuref encoder|decoder|joiner|vocoder|asr-encoder|asr-decoder|asr-joiner /data/model.a7tv /data/input-dir [dynamic]");
    return -EINVAL;
  }
  std::unique_ptr<Work> w(new(std::nothrow) Work);
  if (!w) return -ENOMEM;
  int result=contract(argv[0],w->contract);if(result) return result;
  /* Validate all files before opening the accelerator; explicit command only. */
  for(unsigned i=0;!result && i<w->contract.inputs;++i) {
    void *p=std::malloc(w->contract.input_bytes[i]);
    w->input[i]={p,w->contract.input_bytes[i]};
    result=p?read_input(argv[2],i,p,w->input[i].bytes):-ENOMEM;
  }
  for(unsigned i=0;!result && i<w->contract.outputs;++i) {
    void *p=std::malloc(w->contract.output_bytes[i]);
    w->output[i]={p,w->contract.output_bytes[i]};if(!p)result=-ENOMEM;
  }
  if(result){std::printf("npuref: failed=%d; input/allocation preflight; no hardware run\n",result);return result;}
  int saved=0;
  result=pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,&saved);
  if(result)return -result;
  result=a733_tensor_client_open(argv[1],&w->contract,&w->client);
  struct timespec start{},end{};clock_gettime(CLOCK_MONOTONIC,&start);
  if(!result)result=a733_tensor_client_execute(w->client,w->input,w->output,5000,dynamic?0:1);
  clock_gettime(CLOCK_MONOTONIC,&end);
  if(w->client) {
    int closed=a733_tensor_client_close(w->client);w->client=nullptr;
    if(!result && closed)result=closed;
  }
  if(!result) {
    std::printf("npuref: model=%s backend=VIP2 inputs=%u outputs=%u mode=%s elapsed-us=%lld\n",
      argv[0],w->contract.inputs,w->contract.outputs,dynamic?"dynamic-byte-check":"captured-golden",
      static_cast<long long>(end.tv_sec-start.tv_sec)*1000000+(end.tv_nsec-start.tv_nsec)/1000);
    for(unsigned i=0;i<w->contract.outputs;++i)
      std::printf("npuref output[%u] bytes=%zu raw-crc32=%08lx\n",i,w->output[i].bytes,
                  static_cast<unsigned long>(crc(w->output[i].data,w->output[i].bytes)));
    std::puts("npuref: native tensors returned; not ASR/transcription or end-to-end speech acceptance");
  } else std::printf("npuref: failed=%d; no output publication/fallback/reset/retry\n",result);
  w.reset(); /* fd/owned buffers released before restoring cancellation */
  pthread_setcancelstate(saved,nullptr);
  return result;
}
}
