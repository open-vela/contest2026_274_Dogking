/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include <system/a733_camera_live.h>
#include <nuttx/npu/a733_vip2.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/mount.h>
#include <sys/ioctl.h>

#ifndef LIVE_ROOT
#define LIVE_ROOT "/camram-v204"
#endif
#define LIVE_MODEL LIVE_ROOT "/model.a7pm"
#define LIVE_INPUT LIVE_ROOT "/input.rgb"
#define MODEL_LIMIT (16u*1024u*1024u)
#define RAW_BYTES (3840u*1080u)
static int active,stop_requested,live_stage;
#ifdef CONFIG_A733_CAMERA_PERF_TEST
static int cpu_probe_busy;
#endif
static pthread_mutex_t status_lock=PTHREAD_MUTEX_INITIALIZER;
static uint32_t request_serial,request_token;
static bool identify_job,identify_ready;
static struct a733_live_result identify_result;
static struct
{
  uint32_t seconds,every,frames,inferences,input_crc,model_crc,model_bytes;
  uint64_t start_us,elapsed_us,capture_us,prepare_us,npu_us,display_us;
#ifdef CONFIG_A733_CAMERA_PERF_TEST
  bool optimized,streaming,verify,verified,pipelined,asynchronous;
  unsigned run_calls;
  uint64_t setup_us,loop_start_us,loop_us,warm_start_us,warm_us;
  uint64_t tone_us,crc_us,input_write_us,display_crc_us,display_draw_us,spi_us;
  uint64_t prefetch_us,log_us,frame_wall_us;
  unsigned warm_runs;
  struct a733_npu_live_profile_s npu_parts;
#endif
  int error,cleanup;
  struct a733_camera_tone tone;
  char source[A733_NPU_MODEL_PATH_MAX];
} job;
enum { IDLE,STARTING,COPY_MODEL,CAPTURE,PREPARE,INFERENCE,DISPLAY,CLEANUP,DONE,FAILED };
static const char *const stages[]={"idle","starting","copy-model","capture","prepare","inference","display","cleanup","complete","failed"};
static uint64_t now_us(void)
{struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000u+t.tv_nsec/1000u;}
static void stage(int s){__atomic_store_n(&live_stage,s,__ATOMIC_RELEASE);}
int a733_camera_live_busy(void){return __atomic_load_n(&active,__ATOMIC_ACQUIRE);}
int a733_camera_live_blocked(void){pthread_mutex_lock(&status_lock);int blocked=a733_camera_live_busy()||job.cleanup!=0;pthread_mutex_unlock(&status_lock);return blocked;}
static bool stopping(void){return __atomic_load_n(&stop_requested,__ATOMIC_ACQUIRE)!=0;}
static int control(const char *text)
{
  int fd=open("/dev/a733-camera-control",O_WRONLY);if(fd<0)return -errno;
  size_t length=strlen(text);ssize_t n=write(fd,text,length);
  int ret=n<0?-errno:(size_t)n!=length?-EIO:0;
  if(close(fd)<0 && !ret)ret=-errno;
  return ret;
}
#ifdef CONFIG_A733_CAMERA_PERF_TEST
static int stream_control(const char *text,unsigned timeout_ms)
{
  uint64_t end=now_us()+(uint64_t)timeout_ms*1000;
  do
    {
      int r=control(text);if(!r)return 0;
      if(r!=-EAGAIN && r!=-EBUSY)return r;
      if(stopping() && strcmp(text,"stream-stop"))return -ECANCELED;
      usleep(5000);
    }
  while(now_us()<end);
  return -ETIMEDOUT;
}
#endif
static int read_exact(int fd,uint8_t *data,size_t bytes)
{
  size_t pos=0;
  while(pos<bytes)
    {ssize_t n=read(fd,data+pos,bytes-pos);if(n<0 && errno==EINTR)continue;
     if(n<=0)return n<0?-errno:-EIO;
     pos+=(size_t)n;}
  return 0;
}
static int write_exact(int fd,const uint8_t *data,size_t bytes)
{
  size_t pos=0;
  while(pos<bytes)
    {ssize_t n=write(fd,data+pos,bytes-pos);if(n<0 && errno==EINTR)continue;
     if(n<=0)return n<0?-errno:-EIO;
     pos+=(size_t)n;}
  return 0;
}
static int cache_model(void)
{
  struct stat st;int in=open(job.source,O_RDONLY|O_NONBLOCK);if(in<0)return -errno;
  int ret=0,out=-1;uint8_t *chunk=malloc(65536);uint32_t crc=0,total=0;
  if(!chunk)ret=-ENOMEM;
  if(!ret && fstat(in,&st)<0)ret=-errno;
  if(!ret && (!S_ISREG(st.st_mode) || st.st_size<128 || st.st_size>MODEL_LIMIT))ret=-EMSGSIZE;
  if(!ret){out=open(LIVE_MODEL,O_WRONLY|O_CREAT|O_EXCL,0600);if(out<0)ret=-errno;}
  while(!ret && total<(uint32_t)st.st_size)
    {
      if(stopping()){ret=-ECANCELED;break;}
      size_t n=(uint32_t)st.st_size-total;if(n>65536)n=65536;
      ret=read_exact(in,chunk,n);
      if(!ret && !total)
        {
          /* Exact supported A7PM v3/CID/header/name, not arbitrary graphs
           * with six outputs. Full manifest/region CRCs remain kernel gates. */
          uint32_t fields[12];
          for(unsigned i=0;i<12;++i)
            fields[i]=(uint32_t)chunk[i*4]|(uint32_t)chunk[i*4+1]<<8|
                      (uint32_t)chunk[i*4+2]<<16|(uint32_t)chunk[i*4+3]<<24;
          if(fields[0]!=0x4d503741 || fields[1]!=3 || fields[2]!=128 || fields[3]!=0x1000003b ||
             fields[10]!=(uint32_t)st.st_size || memcmp(chunk+48,"yolov8n_6_pcq_a",16))ret=-EPROTO;
        }
      if(!ret)ret=write_exact(out,chunk,n);
      if(!ret){crc=a733_vision_crc32(chunk,n,crc);total+=(uint32_t)n;}
    }
  if(!ret){uint8_t extra;ssize_t n=read(in,&extra,1);if(n!=0)ret=n<0?-errno:-ESTALE;}
  if(out>=0 && close(out)<0 && !ret)ret=-errno;
  if(close(in)<0 && !ret)ret=-errno;
  free(chunk);
  if(!ret){pthread_mutex_lock(&status_lock);job.model_crc=crc;job.model_bytes=total;pthread_mutex_unlock(&status_lock);}
  return ret;
}
static int write_input(const uint8_t *rgb)
{
  /* Reuse one fixed-size tmpfs allocation instead of truncate/realloc on
   * every frame. No reader starts until this synchronous write has closed. */
  int fd=open(LIVE_INPUT,O_WRONLY|O_CREAT,0600);if(fd<0)return -errno;
  struct stat st;int ret=0;
  if(fstat(fd,&st)<0)ret=-errno;
  else if(!S_ISREG(st.st_mode) || (st.st_size!=0 && st.st_size!=A733_LIVE_RGB_BYTES))ret=-EMSGSIZE;
  if(!ret)ret=write_exact(fd,rgb,A733_LIVE_RGB_BYTES);
  if(close(fd)<0 && !ret)ret=-errno;
  return ret;
}
static uint16_t coord(int32_t centi)
{if(centi<=0)return 0;if(centi>=64000)return 640;return (uint16_t)((centi+50)/100);}
static int infer(int fd,struct a733_npu_run_s *run,struct a733_live_result *result,bool force_legacy)
{
  memset(run,0,sizeof(*run));
  memcpy(run->package_path,LIVE_MODEL,sizeof(LIVE_MODEL));
  memcpy(run->input_path,LIVE_INPUT,sizeof(LIVE_INPUT));run->flags=A733_NPU_RUN_INPUT_FILE;
  int command=A733_NPUIOC_RUN_A7PM;
#ifdef CONFIG_A733_CAMERA_PERF_TEST
  if(job.optimized && !force_legacy)command=A733_NPUIOC_LIVE_RUN;
  pthread_mutex_lock(&status_lock);++job.run_calls;pthread_mutex_unlock(&status_lock);
#else
  (void)force_legacy;
#endif
  uint64_t start=now_us();int r=ioctl(fd,command,(unsigned long)(uintptr_t)run);
  uint64_t elapsed=now_us()-start;
  if(r<0)return -errno;
  if(run->status || run->hardware_status || !(run->irq_value&0x10) || run->input_size!=A733_LIVE_RGB_BYTES ||
     run->input_crc32!=result->input_crc || run->output_count!=6 ||
     run->detection_count>A733_NPU_RUN_MAX_DETECTIONS)return -EPROTO;
  result->kind=A733_LIVE_RESULT;result->npu_us=elapsed>UINT32_MAX?UINT32_MAX:(uint32_t)elapsed;
  result->count=0;
  for(unsigned i=0;i<run->detection_count && result->count<A733_VISION_MAX_BOXES;++i)
    {
      struct a733_vision_message_s b={0};const struct a733_npu_detection_s *d=&run->detections[i];
      b.magic=A733_VISION_MAGIC;b.version=A733_VISION_VERSION;b.type=A733_VISION_DETECTION;
      b.sequence=result->sequence;b.value=result->input_crc;b.index=result->count;
      b.x1=coord(d->x1_centi);b.y1=coord(d->y1_centi);b.x2=coord(d->x2_centi);b.y2=coord(d->y2_centi);
      if(b.x1>=b.x2 || b.y1>=b.y2)continue;
      if(d->score_milli>1000)return -EPROTO;
      b.score_milli=d->score_milli;b.class_id=d->class_id;result->boxes[result->count++]=b;
    }
  for(unsigned i=0;i<result->count;++i)result->boxes[i].count=result->count;
  return a733_live_result_valid(result);
}
#ifdef CONFIG_A733_CAMERA_PERF_TEST
static int verify_resident(int fd,struct a733_npu_run_s *run,struct a733_live_result *result)
{
  struct a733_npu_run_s *reference=malloc(sizeof(*reference));if(!reference)return -ENOMEM;
  int r=infer(fd,run,result,true);if(!r)memcpy(reference,run,sizeof(*reference));
  for(unsigned warm=0;!r && warm<2;++warm)
    {
      if(stopping()){r=-ECANCELED;break;}
      r=infer(fd,run,result,false);if(r)break;
      struct a733_npu_live_profile_s p={.version=A733_NPU_LIVE_ABI};
      if(ioctl(fd,A733_NPUIOC_LIVE_PROFILE,(unsigned long)(uintptr_t)&p)<0){r=-errno;break;}
      if(p.version!=A733_NPU_LIVE_ABI || p.status || p.cache_hit!=warm ||
         p.model_bytes!=job.model_bytes || p.model_crc32!=job.model_crc){r=-EPROTO;break;}
      bool same=run->input_crc32==reference->input_crc32 && run->input_size==reference->input_size &&
        run->output_count==reference->output_count && run->detection_count==reference->detection_count &&
        !memcmp(run->output_crc32,reference->output_crc32,6*sizeof(uint32_t)) &&
        !memcmp(run->detections,reference->detections,run->detection_count*sizeof(run->detections[0]));
      printf("live205 verify phase=%s input=%08lx outputs=%08lx/%08lx/%08lx/%08lx/%08lx/%08lx hit=%lu retained=%lu equal-legacy=%u\n",
        warm?"warm":"cold",(unsigned long)run->input_crc32,(unsigned long)run->output_crc32[0],
        (unsigned long)run->output_crc32[1],(unsigned long)run->output_crc32[2],(unsigned long)run->output_crc32[3],
        (unsigned long)run->output_crc32[4],(unsigned long)run->output_crc32[5],(unsigned long)p.cache_hit,(unsigned long)p.retained_regions,same);
      if(!same)r=-EBADMSG;
    }
  if(!r){pthread_mutex_lock(&status_lock);job.verified=true;pthread_mutex_unlock(&status_lock);puts("live205 verify: PASS same-input legacy/cold/warm all-six-output-CRC and detections; no fallback");}
  free(reference);return r;
}
#endif
struct live_frame_s
{
  struct a733_live_result result;
  uint32_t output_crc32[6];
  uint64_t capture_us,prepare_us,npu_us,display_us;
  bool detect;
#ifdef CONFIG_A733_CAMERA_PERF_TEST
  uint64_t frame_start,tone_us,crc_us,write_us,prefetch_us;
  struct a733_live_display_profile dp;
  struct a733_npu_live_profile_s np;
#endif
};
/* Counts represent completed synchronous SPI flushes, never queue submissions.
 * The renderer is the sole writer of completion counters in async mode. */
static void live_complete(const struct live_frame_s *f)
{
  const struct a733_live_result result=f->result;
  unsigned sequence=result.sequence;bool detect=f->detect;
  uint64_t capture_us=f->capture_us,prepare_us=f->prepare_us;
  uint64_t npu_us=f->npu_us,display_us=f->display_us;
#ifdef CONFIG_A733_CAMERA_PERF_TEST
  uint64_t frame_start=f->frame_start,tone_us=f->tone_us,crc_us=f->crc_us;
  uint64_t write_us=f->write_us,prefetch_us=f->prefetch_us;
  const struct a733_live_display_profile dp=f->dp;
  const struct a733_npu_live_profile_s np=f->np;
#endif
      pthread_mutex_lock(&status_lock);job.frames++;job.inferences+=detect;job.input_crc=result.input_crc;
      if(identify_job && detect && !a733_live_result_valid(&result))
        {identify_result=result;identify_ready=true;}
      job.capture_us+=capture_us;job.prepare_us+=prepare_us;job.npu_us+=npu_us;job.display_us+=display_us;
#ifdef CONFIG_A733_CAMERA_PERF_TEST
      job.tone_us+=tone_us;job.crc_us+=crc_us;job.input_write_us+=write_us;
      job.prefetch_us+=prefetch_us;
      job.display_crc_us+=dp.crc_us;job.display_draw_us+=dp.draw_us;job.spi_us+=dp.flush_us;
      if(detect && job.optimized)
        {
          job.warm_runs+=np.cache_hit;job.npu_parts.import_us=np.import_us;
#define ADD_PART(f) job.npu_parts.f+=np.f
          ADD_PART(prepare_us);ADD_PART(package_us);ADD_PART(map_us);ADD_PART(input_us);ADD_PART(sync_us);
          ADD_PART(hw_exec_us);ADD_PART(output_us);ADD_PART(decode_us);ADD_PART(restore_us);ADD_PART(total_us);
#undef ADD_PART
        }
      if(job.frames==1)job.warm_start_us=now_us();
      job.loop_us=now_us()-job.loop_start_us;
      job.warm_us=job.warm_start_us?now_us()-job.warm_start_us:0;
#endif
      pthread_mutex_unlock(&status_lock);
      bool trace=sequence==1 || sequence%10==0 || detect;
#ifdef CONFIG_A733_CAMERA_PERF_TEST
      uint64_t log_start=now_us();
#endif
#ifdef CONFIG_A733_CAMERA_PERF_TEST
      if(job.optimized)trace=sequence<=2 || sequence%10==0;
#endif
      if(trace)
        printf("live frame=%lu input=%08lx boxes=%lu inferred=%u capture=%llu prepare=%llu npu=%llu display=%llu us\n",
          (unsigned long)sequence,(unsigned long)result.input_crc,(unsigned long)result.count,detect,
          (unsigned long long)capture_us,(unsigned long long)prepare_us,(unsigned long long)npu_us,(unsigned long long)display_us);
      if(detect && trace)
        {
          printf("live npu seq=%lu outputs=%08lx/%08lx/%08lx/%08lx/%08lx/%08lx fallback=0\n",(unsigned long)sequence,
            (unsigned long)f->output_crc32[0],(unsigned long)f->output_crc32[1],(unsigned long)f->output_crc32[2],
            (unsigned long)f->output_crc32[3],(unsigned long)f->output_crc32[4],(unsigned long)f->output_crc32[5]);
#ifdef CONFIG_A733_CAMERA_PERF_TEST
          if(job.optimized)printf("live205 npu seq=%lu hit=%lu retained=%lu prep=%llu package=%llu map=%llu input=%llu sync=%llu hw=%llu output=%llu decode=%llu restore=%llu total=%llu us\n",
            (unsigned long)sequence,(unsigned long)np.cache_hit,(unsigned long)np.retained_regions,
            (unsigned long long)np.prepare_us,(unsigned long long)np.package_us,(unsigned long long)np.map_us,
            (unsigned long long)np.input_us,(unsigned long long)np.sync_us,(unsigned long long)np.hw_exec_us,
            (unsigned long long)np.output_us,(unsigned long long)np.decode_us,(unsigned long long)np.restore_us,(unsigned long long)np.total_us);
#endif
          for(unsigned i=0;i<result.count;++i)
            printf("live det seq=%lu class=%u score=%u box=%u,%u,%u,%u\n",(unsigned long)sequence,
              result.boxes[i].class_id,result.boxes[i].score_milli,result.boxes[i].x1,result.boxes[i].y1,
              result.boxes[i].x2,result.boxes[i].y2);
        }
      /* Yield on every cycle, even when device mocks or a future backend are fast. */
#ifdef CONFIG_A733_CAMERA_PERF_TEST
      pthread_mutex_lock(&status_lock);job.log_us+=now_us()-log_start;
      job.frame_wall_us+=now_us()-frame_start;pthread_mutex_unlock(&status_lock);
#endif

}

#ifdef CONFIG_A733_CAMERA_PERF_TEST
#include "a733_camera_pipeline.inc"
#endif
static void print_status(void)
{
  pthread_mutex_lock(&status_lock);
  uint64_t us=a733_camera_live_busy()?now_us()-job.start_us:job.elapsed_us;
  unsigned s=(unsigned)__atomic_load_n(&live_stage,__ATOMIC_ACQUIRE);
  printf(
#ifdef CONFIG_A733_CAMERA_PERF_TEST
    "camera-live-v209: "
#else
    "camera-live-v204: "
#endif
    "active=%d stop=%d stage=%s error=%d cleanup=%d frames=%lu inference=%lu elapsed-ms=%llu fps=%llu.%03llu input-crc32=%08lx\n",
    a733_camera_live_busy(),stopping(),s<sizeof(stages)/sizeof(*stages)?stages[s]:"invalid",job.error,job.cleanup,
    (unsigned long)job.frames,(unsigned long)job.inferences,(unsigned long long)(us/1000),
    (unsigned long long)(us?(uint64_t)job.frames*1000000/us:0),
    (unsigned long long)(us?((uint64_t)job.frames*1000000000/us)%1000:0),(unsigned long)job.input_crc);
  printf("live-timing-total-us: capture=%llu prepare=%llu npu=%llu display=%llu; model-RAM=%lu/%08lx no-per-frame-SD-files=1; cooperative stop between bounded operations; NOT MP4/ISP/30fps\n",
    (unsigned long long)job.capture_us,(unsigned long long)job.prepare_us,(unsigned long long)job.npu_us,
    (unsigned long long)job.display_us,(unsigned long)job.model_bytes,(unsigned long)job.model_crc);
#ifdef CONFIG_A733_CAMERA_PERF_TEST
  printf("live205: optimized=%u sensor-stream=%u setup-us=%llu loop-us=%llu warm-us=%llu warm-fps-milli=%llu inference-fps-milli=%llu warm-npu-runs=%u\n",
    job.optimized,job.streaming,(unsigned long long)job.setup_us,(unsigned long long)job.loop_us,
    (unsigned long long)job.warm_us,(unsigned long long)(job.warm_us && job.frames>1?(uint64_t)(job.frames-1)*1000000000/job.warm_us:0),
    (unsigned long long)(job.loop_us?(uint64_t)job.inferences*1000000000/job.loop_us:0),job.warm_runs);
  printf("live205 CPU-us: tone=%llu input-crc=%llu tmp-input=%llu display-crc=%llu draw=%llu spi-flush=%llu\n",
    (unsigned long long)job.tone_us,(unsigned long long)job.crc_us,(unsigned long long)job.input_write_us,
    (unsigned long long)job.display_crc_us,(unsigned long long)job.display_draw_us,(unsigned long long)job.spi_us);
  printf("live205 overhead-us: prefetch-command=%llu logging=%llu successful-frames-wall=%llu; sensor capture overlaps CPU only in live-stream\n",
    (unsigned long long)job.prefetch_us,(unsigned long long)job.log_us,(unsigned long long)job.frame_wall_us);
  printf("live205 validation: requested=%u verified=%u run-ioctls=%u; verify adds two first-frame runs, NOT displayed inference frames\n",job.verify,job.verified,job.run_calls);
  printf("live209: rgb-pipeline=%u input-template=exact-full-region-dynamic-only ISA=runtime-qualified CRC=fallback-preserved no-DVFS-writes\n",job.pipelined);
  printf("live205 NPU-us: import=%llu prepare=%llu package=%llu map=%llu input=%llu sync=%llu hw-exec=%llu output=%llu decode=%llu restore=%llu total=%llu; prepare includes package/map/input/sync; hw-exec includes init/wait\n",
    (unsigned long long)job.npu_parts.import_us,(unsigned long long)job.npu_parts.prepare_us,
    (unsigned long long)job.npu_parts.package_us,(unsigned long long)job.npu_parts.map_us,
    (unsigned long long)job.npu_parts.input_us,(unsigned long long)job.npu_parts.sync_us,
    (unsigned long long)job.npu_parts.hw_exec_us,(unsigned long long)job.npu_parts.output_us,
    (unsigned long long)job.npu_parts.decode_us,(unsigned long long)job.npu_parts.restore_us,(unsigned long long)job.npu_parts.total_us);
#endif
  pthread_mutex_unlock(&status_lock);
}
static void stop_signal(int signo)
{(void)signo;__atomic_store_n(&stop_requested,1,__ATOMIC_RELEASE);}
static int worker(int argc,char **argv)
{
#ifdef CONFIG_A733_PRODUCT_V1
  /* Dedicated child group must not retain a disconnected SSH socket. */
  int console=open("/dev/a7zconsole",O_WRONLY);
  if(console>=0){(void)dup2(console,1);(void)dup2(console,2);close(console);}
#endif
  (void)argc;(void)argv;
  struct sigaction action={0};action.sa_handler=stop_signal;sigemptyset(&action.sa_mask);
  sigaction(SIGINT,&action,NULL);sigaction(SIGTERM,&action,NULL);
  int ret=0,cleanup=0,npu=-1;bool directory=false,mounted=false,qualified=false,lcd=false;
  uint8_t *raw=malloc(RAW_BYTES),*rgb=malloc(A733_LIVE_RGB_BYTES);
  uint8_t *owned_rgb=rgb;
  struct a733_npu_run_s *run=calloc(1,sizeof(*run));
#ifdef CONFIG_A733_CAMERA_PERF_TEST
  struct a733_camera_tone_plan *plan=NULL;bool owns_stream=false;
  struct live_pipe_s *pipeline=NULL;bool retained_buffers=false;
  if(job.optimized)ret=live_cpu_bind("infer-display",6);
#endif
  if(!raw || !rgb || !run)ret=-ENOMEM;
  if(!ret && job.every)
    {
      stage(COPY_MODEL);
      if(mkdir(LIVE_ROOT,0700)<0)ret=-errno;else directory=true;
      if(!ret){if(mount(NULL,LIVE_ROOT,"tmpfs",0,NULL)<0)ret=-errno;else mounted=true;}
      if(!ret){struct statfs fs;if(statfs(LIVE_ROOT,&fs)<0)ret=-errno;else if(fs.f_type!=TMPFS_MAGIC)ret=-EXDEV;else qualified=true;}
      if(!ret)ret=cache_model();
      if(!ret){npu=open("/dev/npu0",O_RDWR);if(npu<0)ret=-errno;}
#ifdef CONFIG_A733_CAMERA_PERF_TEST
      if(!ret && job.optimized)
        {
          struct a733_npu_live_prepare_s p={.version=A733_NPU_LIVE_ABI};
          memcpy(p.path,LIVE_MODEL,sizeof(LIVE_MODEL));
          if(ioctl(npu,A733_NPUIOC_LIVE_PREPARE,(unsigned long)(uintptr_t)&p)<0)ret=-errno;
          if(!ret && (p.status || p.bytes!=job.model_bytes || p.crc32!=job.model_crc))ret=-EPROTO;
        }
#endif
    }
  if(!ret && !stopping()
#ifdef CONFIG_A733_CAMERA_PERF_TEST
     && !job.asynchronous
#endif
    ){ret=a733_display_live_begin();if(!ret)lcd=true;}
  struct a733_camera_image source={.data=raw,.bytes=RAW_BYTES,.width=1920,.height=1080,.stride=3840,
    .pixel=A733_CAMERA_RAW10_LE16,.bayer=A733_CAMERA_BGGR};
#ifdef CONFIG_A733_CAMERA_PERF_TEST
  if(!ret && job.optimized)ret=a733_camera_tone_plan_create(&source,rgb,A733_LIVE_RGB_BYTES,640,640,114,&job.tone,&plan);
  if(!ret && !stopping() && job.streaming){ret=control("stream-start");owns_stream=!ret;}
  pthread_mutex_lock(&status_lock);job.loop_start_us=now_us();job.setup_us=job.loop_start_us-job.start_us;pthread_mutex_unlock(&status_lock);
#endif
  uint64_t deadline=now_us()+(uint64_t)job.seconds*1000000;
#ifdef CONFIG_A733_CAMERA_PERF_TEST
  if(!ret && !stopping() && job.pipelined)
    {
      pipeline=calloc(1,sizeof(*pipeline));if(!pipeline)ret=-ENOMEM;
      else ret=pipe_start(pipeline,&source,rgb,plan,deadline);
    }
#endif
  for(uint32_t sequence=1;!ret && !stopping() && now_us()<deadline && sequence<=A733_LIVE_MAX_FRAMES;++sequence)
    {
      uint64_t t=now_us(),capture_us,prepare_us,npu_us=0,display_us;
#ifdef CONFIG_A733_CAMERA_PERF_TEST
      uint64_t frame_start=t,prefetch_us=0;
      uint64_t tone_us=0,crc_us=0,write_us=0;
      struct a733_live_display_profile dp={0};
      struct a733_npu_live_profile_s np={.version=A733_NPU_LIVE_ABI};
      struct pipe_slot_s *pipe_slot=NULL;
#endif
      struct a733_live_result result={.sequence=sequence};
#ifdef CONFIG_A733_CAMERA_PERF_TEST
      if(pipeline)
        {
          int q=pipe_take(pipeline,sequence,&pipe_slot);
          if(q){ret=q==1?0:q;break;}
          rgb=pipe_slot->rgb;result=pipe_slot->result;
          capture_us=pipe_slot->capture_us;prepare_us=pipe_slot->prepare_us;
          tone_us=pipe_slot->tone_us;crc_us=pipe_slot->crc_us;prefetch_us=pipe_slot->prefetch_us;
        }
      else
#endif
        {
      stage(CAPTURE);
#ifdef CONFIG_A733_CAMERA_PERF_TEST
      if(job.streaming)ret=stream_control("stream-take",3000);
      else
#endif
        {ret=control("prepare");if(!ret)ret=control("frame-experiment");}
      int fd=-1;if(!ret){fd=open("/dev/a733-camera-frame",O_RDONLY);if(fd<0)ret=-errno;}
      if(fd>=0){ret=read_exact(fd,raw,RAW_BYTES);if(close(fd)<0 && !ret)ret=-errno;}
      capture_us=now_us()-t;if(ret || stopping())break;
#ifdef CONFIG_A733_CAMERA_PERF_TEST
      /* Kernel DMA buffer and this copied RAW are distinct. Exactly one
       * prefetch may overlap CPU/NPU/display; no unbounded frame queue. */
      if(job.streaming){t=now_us();ret=stream_control("stream-prefetch",3000);prefetch_us=now_us()-t;if(ret)break;}
#endif
      stage(PREPARE);t=now_us();source.sequence=sequence;source.timestamp_us=t;
      struct a733_camera_transform transform;
#ifdef CONFIG_A733_CAMERA_PERF_TEST
      if(job.optimized)ret=a733_camera_tone_plan_render(plan,&source,&transform);
      else
#endif
        ret=a733_camera_letterbox_tone(&source,rgb,A733_LIVE_RGB_BYTES,640,640,114,&job.tone,&transform);
#ifdef CONFIG_A733_CAMERA_PERF_TEST
      tone_us=now_us()-t;uint64_t crc_start=now_us();
#endif
      if(!ret)result.input_crc=a733_vision_crc32(rgb,A733_LIVE_RGB_BYTES,0);
#ifdef CONFIG_A733_CAMERA_PERF_TEST
      crc_us=now_us()-crc_start;
#endif
      prepare_us=now_us()-t;if(ret || stopping())break;
        }
      bool detect=job.every && (sequence-1)%job.every==0;
      display_us=0;
#ifdef CONFIG_A733_CAMERA_PERF_TEST
      if(!job.asynchronous && (!job.optimized || !detect))
#endif
        {
          stage(DISPLAY);t=now_us();
#ifdef CONFIG_A733_CAMERA_PERF_TEST
          if(job.optimized)ret=a733_display_live_present(rgb,A733_LIVE_RGB_BYTES,&result,&dp);
          else
#endif
            ret=a733_display_live_frame(rgb,A733_LIVE_RGB_BYTES,&result);
          display_us=now_us()-t;if(ret || stopping())break;
        }
      if(detect)
        {
#ifdef CONFIG_A733_CAMERA_PERF_TEST
          if(job.asynchronous)pipe_mark_npu(pipeline,true);
#endif
          stage(INFERENCE);t=now_us();ret=write_input(rgb);
#ifdef CONFIG_A733_CAMERA_PERF_TEST
          write_us=now_us()-t;
#endif
          if(!ret)
            {
#ifdef CONFIG_A733_CAMERA_PERF_TEST
              if(job.verify && !job.verified)ret=verify_resident(npu,run,&result);
              else
#endif
                ret=infer(npu,run,&result,false);
            }
#ifdef CONFIG_A733_CAMERA_PERF_TEST
          if(job.optimized)
            {
              int q=ioctl(npu,A733_NPUIOC_LIVE_PROFILE,(unsigned long)(uintptr_t)&np);if(q<0 && !ret)ret=-errno;
              if(!ret && (np.version!=A733_NPU_LIVE_ABI || np.status || np.model_bytes!=job.model_bytes || np.model_crc32!=job.model_crc))ret=-EPROTO;
            }
#endif
          npu_us=now_us()-t;
#ifdef CONFIG_A733_CAMERA_PERF_TEST
          if(job.asynchronous)pipe_mark_npu(pipeline,false);
#endif
          if(ret || stopping())break;
#ifdef CONFIG_A733_CAMERA_PERF_TEST
          if(!job.asynchronous)
#endif
            {
          stage(DISPLAY);t=now_us();
#ifdef CONFIG_A733_CAMERA_PERF_TEST
          if(job.optimized)ret=a733_display_live_present(rgb,A733_LIVE_RGB_BYTES,&result,&dp);
          else
#endif
            ret=a733_display_live_frame(rgb,A733_LIVE_RGB_BYTES,&result);
          display_us+=now_us()-t;if(ret)break;
            }
        }
      struct live_frame_s f={.result=result,.detect=detect,.capture_us=capture_us,
        .prepare_us=prepare_us,.npu_us=npu_us,.display_us=display_us};
      memcpy(f.output_crc32,run->output_crc32,sizeof(f.output_crc32));
#ifdef CONFIG_A733_CAMERA_PERF_TEST
      f.frame_start=frame_start;f.tone_us=tone_us;f.crc_us=crc_us;
      f.write_us=write_us;f.prefetch_us=prefetch_us;f.dp=dp;f.np=np;
      if(job.asynchronous) {ret=pipe_submit(pipeline,pipe_slot,&f);if(ret)break;}
      else
#endif
        {
          live_complete(&f);
#ifdef CONFIG_A733_CAMERA_PERF_TEST
          if(pipeline)pipe_release(pipeline,pipe_slot);
#endif
        }
      usleep(1000);
      if(identify_job)break; /* A completed, displayed fresh frame, not a timer. */
    }
  stage(CLEANUP);
  if(ret==-ECANCELED && stopping())ret=0;
#ifdef CONFIG_A733_CAMERA_PERF_TEST
  if(pipeline)
    {
      int c=pipe_finish(pipeline,!ret && !stopping());
      if(c){retained_buffers=true;if(!cleanup)cleanup=c;
#ifdef CONFIG_A733_PRODUCT_V1
        pthread_mutex_lock(&status_lock);job.error=-EOWNERDEAD;job.cleanup=cleanup;job.elapsed_us=now_us()-job.start_us;
        pthread_mutex_unlock(&status_lock);stage(FAILED);print_status();
        puts("camera product-v1: renderer/producer ownership uncertain; retaining SAME task, NPU fd, SPI and buffers; REBOOT REQUIRED");
        for(;;)usleep(100000);
#endif
      }
      else {if(!ret)ret=pipeline->error;if(!cleanup)cleanup=pipeline->cleanup;pipe_destroy(pipeline);pipeline=NULL;}
    }
  if(ret==-ECANCELED && stopping())ret=0;
  if(owns_stream && !retained_buffers){int c=stream_control("stream-stop",7000);if(c && !cleanup)cleanup=c;}
  if(!retained_buffers)a733_camera_tone_plan_destroy(plan);
#endif
  if(lcd){int r=a733_display_live_end();if(r && !cleanup)cleanup=r;}
  if(npu>=0 && close(npu)<0 && !cleanup)cleanup=-errno;
  if(mounted && !qualified){cleanup=-EXDEV;directory=false;}
  if(mounted && qualified)
    {
      /* Exactly two owned tmpfs files. No recursive removal or reset/abort. */
      if(unlink(LIVE_INPUT)<0 && errno!=ENOENT && !cleanup)cleanup=-errno;
      if(unlink(LIVE_MODEL)<0 && errno!=ENOENT && !cleanup)cleanup=-errno;
      if(umount(LIVE_ROOT)<0){if(!cleanup)cleanup=-errno;directory=false;}
    }
  /* NuttX umount normally removes the empty pseudofs mountpoint inode. */
  if(directory && rmdir(LIVE_ROOT)<0 && errno!=ENOENT && !cleanup)cleanup=-errno;
  free(run);
#ifdef CONFIG_A733_CAMERA_PERF_TEST
  if(!retained_buffers)
#endif
    {free(owned_rgb);free(raw);}
  pthread_mutex_lock(&status_lock);job.error=ret;job.cleanup=cleanup;job.elapsed_us=now_us()-job.start_us;
  pthread_mutex_unlock(&status_lock);stage(ret || cleanup?FAILED:DONE);
  __atomic_store_n(&active,0,__ATOMIC_RELEASE);print_status();return 0;
}
static int live_command(int argc,char **argv,const struct a733_camera_tone *tone,uint32_t *token)
{
#ifdef CONFIG_A733_CAMERA_PERF_TEST
  if(argc==2 && !strcmp(argv[1],"live-cpu"))return live_cpu_probe();
#endif
  if(argc==2 && !strcmp(argv[1],"live-status")){print_status();return 0;}
  if(argc==2 && !strcmp(argv[1],"live-stop"))
    {__atomic_store_n(&stop_requested,1,__ATOMIC_RELEASE);puts("live: stop requested; wait for active=0 and cleanup=0, no forced DMA/NPU reset");return 0;}
  if(argc<3 || argc>5)return -EINVAL;
  uint32_t values[2]={0,1};
  for(int i=0;i<2 && i+2<argc;++i)
    {char *end;const char *s=argv[i+2];if(!*s || *s<'0' || *s>'9')return -EINVAL;
     errno=0;unsigned long n=strtoul(s,&end,10);if(errno || *end || n>UINT32_MAX)return -EINVAL;values[i]=n;}
  if(!values[0] || values[0]>A733_LIVE_MAX_SECONDS || values[1]>60)return -ERANGE;
  const char *model=argc==5?argv[4]:"/data/npu/yolov8n-pcq-v3.a7pm";
  if(strlen(model)>=sizeof(job.source) || model[0]!='/' || strstr(model,"..") || strchr(model,'\\'))return -EINVAL;
  if(a733_camera_tone_valid(tone))return -EINVAL;
#ifdef CONFIG_A733_PRODUCT_V1
  int reserved=a733_product_camera_begin();if(reserved)return reserved;
#endif
  pthread_mutex_lock(&status_lock);
  int blocked=a733_camera_live_busy()?-EBUSY:job.cleanup?-EIO:0;
#ifdef CONFIG_A733_CAMERA_PERF_TEST
  if(cpu_probe_busy)blocked=-EBUSY;
#endif
  /* A failed cleanup is quarantined. Do not adopt/delete a prior mount. */
  if(blocked){pthread_mutex_unlock(&status_lock);
#ifdef CONFIG_A733_PRODUCT_V1
    a733_product_start_unlock();
#endif
    return blocked;}
  memset(&job,0,sizeof(job));job.seconds=values[0];job.every=values[1];job.tone=*tone;
  identify_job=token!=NULL;identify_ready=false;memset(&identify_result,0,sizeof(identify_result));
  if(!++request_serial)++request_serial;
  request_token=request_serial;if(token)*token=request_token;
#ifdef CONFIG_A733_CAMERA_PERF_TEST
  job.optimized=strcmp(argv[1],"live-legacy")!=0;
  job.asynchronous=!strcmp(argv[1],"live-async") || !strcmp(argv[1],"live-async-verify");
  job.streaming=job.asynchronous || !strcmp(argv[1],"live-stream") || !strcmp(argv[1],"live-pipe");
  job.pipelined=job.asynchronous || !strcmp(argv[1],"live-pipe");
  job.verify=!strcmp(argv[1],"live-verify") || !strcmp(argv[1],"live-async-verify");
#endif
  memcpy(job.source,model,strlen(model)+1);job.start_us=now_us();stage(STARTING);
  __atomic_store_n(&stop_requested,0,__ATOMIC_RELEASE);__atomic_store_n(&active,1,__ATOMIC_RELEASE);
  /* Independent NuttX task group: a detached pthread would be terminated
   * when the launching camera CLI task exits. Globals are flat-build only. */
  int pid=task_create("camera-live",100,65536,worker,NULL);int r=pid<0?errno:0;
  if(r){job.error=-r;stage(FAILED);__atomic_store_n(&active,0,__ATOMIC_RELEASE);}
  pthread_mutex_unlock(&status_lock);
#ifdef CONFIG_A733_PRODUCT_V1
  a733_product_start_unlock();
#endif
  if(!r)printf("live: accepted seconds=%lu detect-every=%lu (0=preview), RAM pipeline; use camera live-status / live-stop\n",(unsigned long)values[0],(unsigned long)values[1]);
  return -r;
}
int a733_camera_live_command(int argc,char **argv,const struct a733_camera_tone *tone)
{return live_command(argc,argv,tone,NULL);}
int a733_camera_identify_start(const char *model,uint32_t *token)
{
  /* Same v203 qualified preview preset; no unqualified ISP or power changes. */
  const struct a733_camera_tone tone={64,512,512,256,256,180};
  char *args[]={"camera","identify","30","1",(char *)model};
  if(!token || !model)return -EINVAL;
  return live_command(5,args,&tone,token);
}
int a733_camera_identify_poll(uint32_t token,struct a733_live_result *result)
{
  if(!result)return -EINVAL;
  pthread_mutex_lock(&status_lock);int rc=0;
  if(!token || token!=request_token || !identify_job)rc=-ESTALE;
  else if(a733_camera_live_busy())rc=-EAGAIN;
  else if(job.cleanup)rc=-EOWNERDEAD;
  else if(job.error)rc=job.error;
  else if(!identify_ready)rc=-ENODATA;
  else *result=identify_result;
  pthread_mutex_unlock(&status_lock);return rc;
}
int a733_camera_identify_stop(uint32_t token)
{
  pthread_mutex_lock(&status_lock);
  int rc=token && token==request_token && identify_job?0:-ESTALE;
  if(!rc)__atomic_store_n(&stop_requested,1,__ATOMIC_RELEASE);
  pthread_mutex_unlock(&status_lock);return rc;
}
