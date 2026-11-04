/* SPDX-License-Identifier: Apache-2.0 */
#include "voice_acoustic.hxx"
#include "voice_compute.hxx"
#include "voice_load_log.hxx"
#include "third_party/onnxruntime/include/onnxruntime_c_api.h"
#include <array>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <new>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace aipet {
struct VoiceAcoustic {
  const OrtApi *api=nullptr;
  OrtEnv *env=nullptr;
  OrtSession *session=nullptr;
  OrtMemoryInfo *memory=nullptr;
  OrtValue *active=nullptr;
  bool poisoned=false;
  std::unique_ptr<unsigned char[]> model;
};
namespace {
long long clock_us() {
  timespec t{};
  if(clock_gettime(CLOCK_MONOTONIC,&t))return -(errno?errno:EIO);
  return static_cast<long long>(t.tv_sec)*1000000+t.tv_nsec/1000;
}
struct Monitor {
  const OrtApi *api;
  OrtRunOptions *options=nullptr;
  long long deadline;
  VoiceAcousticStop stop;
  std::atomic<unsigned> refs{1};
  std::atomic<bool> done{false};
  std::atomic<int> error{0};
};
void release(Monitor *m) {
  if(m->refs.fetch_sub(1)==1) {
    if(m->options)m->api->ReleaseRunOptions(m->options);
    delete m;
  }
}
int poll(Monitor *m) {
  long long t=clock_us();
  if(t<0)return static_cast<int>(t);
  if(t>=m->deadline)return -ETIMEDOUT;
  int rc=m->stop?m->stop():0;
  return rc<0?rc:rc?-ECANCELED:0;
}
void *watch(void *cookie) {
  auto *m=static_cast<Monitor *>(cookie);
  while(!m->done.load()) {
    int rc=poll(m);
    if(rc) {
      m->error.store(rc);
      if(auto *error=m->api->RunOptionsSetTerminate(m->options))m->api->ReleaseStatus(error);
      break;
    }
    usleep(5000);
  }
  release(m);return nullptr;
}
constexpr std::size_t model_bytes=65033312;
constexpr std::array<uint32_t,256> table() {
  std::array<uint32_t,256> t{};
  for(unsigned i=0;i<256;i++) {
    uint32_t x=i;for(unsigned b=0;b<8;b++)x=(x>>1)^((x&1)?0xedb88320u:0u);t[i]=x;
  }
  return t;
}
uint32_t crc(const unsigned char *p,std::size_t n,uint32_t c=0) {
  static constexpr auto t=table();
  while(n--)c=t[(c^*p++)&255u]^(c>>8);
  return c;
}
int status(VoiceAcoustic *s,OrtStatus *error) {
  if(!error)return 0;
  auto code=s->api->GetErrorCode(error);
  std::printf("acoustic: ORT error=%d %.160s\n",int(code),s->api->GetErrorMessage(error));
  s->api->ReleaseStatus(error);
  return code==ORT_INVALID_ARGUMENT ? -EINVAL : code==ORT_NO_SUCHFILE ? -ENOENT :
         code==ORT_NOT_IMPLEMENTED ? -ENOTSUP : -EIO;
}
int model_load(const char *name,VoiceAcoustic *s) {
  VoiceLoadLog load("acoustic-read-crc",name);
  int fd=open(name,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return load.finish(-errno);
  struct stat st{};int rc=0;
  if(fstat(fd,&st)<0)rc=-errno;
  else if(!S_ISREG(st.st_mode)||st.st_size!=static_cast<off_t>(model_bytes))rc=-EBADMSG;
  if(!rc) {
    s->model.reset(new(std::nothrow) unsigned char[model_bytes]);
    if(!s->model)rc=-ENOMEM;
  }
  std::size_t offset=0,next_report=8*1024*1024;
  uint32_t checksum=0;
  while(!rc && offset<model_bytes) {
    std::size_t n=model_bytes-offset;if(n>1024*1024)n=1024*1024;
    ssize_t got=read(fd,s->model.get()+offset,n);
    if(got<0 && errno==EINTR)continue;
    if(got<=0)rc=got<0?-errno:-EIO;
    else {
      checksum=crc(s->model.get()+offset,static_cast<std::size_t>(got),checksum);
      offset+=static_cast<std::size_t>(got);
      if(offset>=next_report || offset==model_bytes) {
        load.progress(offset,model_bytes);next_report=offset+8*1024*1024;
      }
    }
  }
  if(close(fd)<0 && !rc)rc=-errno;
  // Corruption guard for the separately SHA256-locked v183 .ort asset.
  // CRC is NOT an authentication mechanism or an arbitrary-model sandbox.
  if(!rc && checksum!=0x4f23c1a5u)rc=-EBADMSG;
  return load.finish(rc);
}
int io_contract(VoiceAcoustic *s) {
  std::size_t inputs=0,outputs=0;
  int rc=status(s,s->api->SessionGetInputCount(s->session,&inputs));
  if(!rc)rc=status(s,s->api->SessionGetOutputCount(s->session,&outputs));
  if(rc)return rc;
  if(inputs!=4 || outputs!=1)return -EPROTO;
  OrtAllocator *allocator=nullptr;
  rc=status(s,s->api->GetAllocatorWithDefaultOptions(&allocator));if(rc)return rc;
  const char *names[]={"x","x_length","noise_scale","length_scale"};
  for(unsigned i=0;!rc && i<4;i++) {
    char *name=nullptr;OrtTypeInfo *type=nullptr;const OrtTensorTypeAndShapeInfo *tensor=nullptr;
    rc=status(s,s->api->SessionGetInputName(s->session,i,allocator,&name));
    if(!rc && (!name || std::strcmp(name,names[i])))rc=-EPROTO;
    if(name)allocator->Free(allocator,name);
    if(!rc)rc=status(s,s->api->SessionGetInputTypeInfo(s->session,i,&type));
    if(!rc)rc=status(s,s->api->CastTypeInfoToTensorInfo(type,&tensor));
    ONNXTensorElementDataType dtype{};std::size_t rank=0;
    if(!rc)rc=status(s,s->api->GetTensorElementType(tensor,&dtype));
    if(!rc)rc=status(s,s->api->GetDimensionsCount(tensor,&rank));
    if(!rc && (dtype!=(i<2?ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) ||
                rank!=(i==0?2u:1u)))rc=-EPROTO;
    if(type)s->api->ReleaseTypeInfo(type);
  }
  return rc;
}
}
int voice_acoustic_open(const char *model,VoiceAcoustic **out) {
  if(!out)return -EINVAL;
  *out=nullptr;if(!model)return -EINVAL;
  auto *s=new(std::nothrow) VoiceAcoustic;if(!s)return -ENOMEM;
  const OrtApiBase *base=OrtGetApiBase();
  s->api=base?base->GetApi(ORT_API_VERSION):nullptr;
  if(!s->api){delete s;return -ENOTSUP;}
  int rc=model_load(model,s);OrtSessionOptions *options=nullptr;
  if(!rc)rc=status(s,s->api->CreateEnv(ORT_LOGGING_LEVEL_ERROR,"aipet-acoustic",&s->env));
  if(!rc)rc=status(s,s->api->CreateSessionOptions(&options));
  if(!rc)rc=status(s,s->api->SetIntraOpNumThreads(options,1));
  if(!rc)rc=status(s,s->api->SetInterOpNumThreads(options,1));
  if(!rc)rc=status(s,s->api->SetSessionGraphOptimizationLevel(options,ORT_DISABLE_ALL));
  if(!rc)rc=status(s,s->api->AddSessionConfigEntry(options,"session.use_ort_model_bytes_directly","1"));
  if(!rc)rc=status(s,s->api->AddSessionConfigEntry(options,"session.use_ort_model_bytes_for_initializers","1"));
  if(!rc) {
    VoiceLoadLog load("acoustic-ORT-session",model);
    rc=status(s,s->api->CreateSessionFromArray(s->env,s->model.get(),model_bytes,options,&s->session));
    load.finish(rc);
  }
  if(options)s->api->ReleaseSessionOptions(options);
  if(!rc)rc=status(s,s->api->CreateCpuMemoryInfo(OrtArenaAllocator,OrtMemTypeDefault,&s->memory));
  if(!rc)rc=io_contract(s);
  if(rc){voice_acoustic_close(s);return rc;}
  *out=s;return 0;
}
static int acoustic_run(VoiceAcoustic *s,const int64_t *ids,std::size_t count,
                        float noise,float length,VoiceAcousticMel *out,
                        long long deadline,VoiceAcousticStop stop) {
  if(!out)return -EINVAL;
  if((s && s->active) || out->tensor)return -EBUSY;
  *out={};
  if(!s || !ids || !count || count>128 || !std::isfinite(noise) || noise<0 || noise>1 ||
     !std::isfinite(length) || length<0.5f || length>2)return -EINVAL;
  if(s->poisoned)return -EIO;
  long long start=clock_us();if(start<0)return static_cast<int>(start);
  if(!deadline)deadline=start+120000000LL;
  if(deadline<=start)return -ETIMEDOUT;
  if(deadline-start>120000000LL)return -EINVAL;
  for(std::size_t i=0;i<count;i++)if(ids[i]<0 || ids[i]>65535)return -ERANGE;
  int64_t shapes[4][2]={{1,static_cast<int64_t>(count)},{1,0},{1,0},{1,0}};
  int64_t x_length=static_cast<int64_t>(count);
  void *data[]={const_cast<int64_t *>(ids),&x_length,&noise,&length};
  std::size_t bytes[]={count*8u,8u,4u,4u};OrtValue *inputs[4]{};int rc=0;
  for(unsigned i=0;!rc && i<4;i++) {
    auto dtype=i<2?ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
    rc=status(s,s->api->CreateTensorWithDataAsOrtValue(s->memory,data[i],bytes[i],shapes[i],i?1:2,dtype,&inputs[i]));
  }
  const char *names[]={"x","x_length","noise_scale","length_scale"};
  const char *outputs[]={"mel"};OrtValue *result=nullptr;
  auto *monitor=new(std::nothrow) Monitor{s->api,nullptr,deadline,stop};
  if(!monitor && !rc)rc=-ENOMEM;
  pthread_t thread{};bool started=false;
  if(!rc)rc=poll(monitor);
  if(!rc)rc=status(s,s->api->CreateRunOptions(&monitor->options));
  if(!rc) {
    pthread_attr_t attr;int e=pthread_attr_init(&attr);
    if(!e) {
      e=pthread_attr_setstacksize(&attr,65536);
      if(!e) {
        monitor->refs.fetch_add(1);
        e=pthread_create(&thread,&attr,watch,monitor);
        if(e)release(monitor);else started=true;
      }
      pthread_attr_destroy(&attr);
    }
    if(e)rc=-e;
  }
  if(!rc)rc=status(s,s->api->Run(s->session,monitor->options,names,
      const_cast<const OrtValue *const *>(inputs),4,outputs,1,&result));
  if(monitor) {
    monitor->done.store(true);
    if(started) {
      int joined=pthread_join(thread,nullptr);
      if(joined){s->poisoned=true;if(!rc)rc=-joined;}
    }
    // Even a join failure cannot release a monitor/RunOptions still owned by
    // its thread. No caller stack/cookie or Session pointer is stored there.
    int stopped=monitor->error.load();if(!stopped)stopped=poll(monitor);
    if(stopped)rc=stopped;
    release(monitor);
  }
  for(auto *v:inputs)if(v)s->api->ReleaseValue(v);
  OrtTensorTypeAndShapeInfo *info=nullptr;ONNXTensorElementDataType dtype{};
  std::size_t rank=0;int64_t dims[3]{};void *mel=nullptr;
  if(!rc)rc=status(s,s->api->GetTensorTypeAndShape(result,&info));
  if(!rc)rc=status(s,s->api->GetTensorElementType(info,&dtype));
  if(!rc)rc=status(s,s->api->GetDimensionsCount(info,&rank));
  if(!rc && (rank!=3 || dtype!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT))rc=-EPROTO;
  if(!rc)rc=status(s,s->api->GetDimensions(info,dims,3));
  if(!rc && (dims[0]!=1 || dims[1]!=80 || dims[2]<1 || dims[2]>861))rc=-E2BIG;
  if(!rc)rc=status(s,s->api->GetTensorMutableData(result,&mel));
  if(info)s->api->ReleaseTensorTypeAndShapeInfo(info);
  if(!rc)for(std::size_t i=0;i<80*static_cast<std::size_t>(dims[2]);i++)
    if(!std::isfinite(static_cast<const float *>(mel)[i])){rc=-EILSEQ;break;}
  if(rc){if(result)s->api->ReleaseValue(result);return rc;}
  s->active=result;*out={static_cast<const float *>(mel),static_cast<unsigned>(dims[2]),result};return 0;
}
int voice_acoustic_run(VoiceAcoustic *s,const int64_t *ids,std::size_t count,
    float noise,float length,VoiceAcousticMel *out,long long deadline,VoiceAcousticStop stop) {
  VoiceCompute compute;int rc=compute.begin();
  if(!rc)rc=acoustic_run(s,ids,count,noise,length,out,deadline,stop);
  const bool produced=rc==0;
  rc=compute.finish(rc);
  if(rc && produced && s && out && out->tensor==s->active && out->tensor)
    voice_acoustic_mel_release(s,out);
  return rc;
}
void voice_acoustic_mel_release(VoiceAcoustic *s,VoiceAcousticMel *mel) {
  if(!s || !mel)return;
  if(mel->tensor && mel->tensor!=s->active)return;
  if(mel->tensor && mel->tensor==s->active){s->api->ReleaseValue(s->active);s->active=nullptr;}
  *mel={};
}
void voice_acoustic_close(VoiceAcoustic *s) {
  if(!s)return;
  if(s->active)s->api->ReleaseValue(s->active);
  if(s->memory)s->api->ReleaseMemoryInfo(s->memory);
  if(s->session)s->api->ReleaseSession(s->session);
  if(s->env)s->api->ReleaseEnv(s->env);
  delete s;
}
}
