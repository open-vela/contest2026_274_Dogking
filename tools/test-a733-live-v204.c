/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/statfs.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <sched.h>
#include <system/a733_camera_live.h>
#include <nuttx/npu/a733_vip2.h>
#ifndef TMPFS_MAGIC
#define TMPFS_MAGIC 0x01021994
#endif
static int test_task(int argc,char **argv);
static int (*pending_task)(int,char**);
static int fake_task_create(const char *name,int priority,int stack,int (*entry)(int,char**),char *const argv[])
{(void)name;(void)priority;(void)stack;(void)argv;if(getenv("FAIL_TASK")){errno=ENOMEM;return -1;}pending_task=entry;return 123;}
static unsigned captures,displayed,inferred,closes;
static bool lcd_owned,mount_owned;
static pthread_t lcd_thread;
#ifdef CONFIG_A733_CAMERA_PERF_TEST
static unsigned thread_calls,render_calls;
static int mock_npu_active,mock_display_active,mock_overlaps;
static int fake_pthread_create(pthread_t *t,const pthread_attr_t *a,void *(*f)(void*),void *arg)
{++thread_calls;if(getenv("FAIL_PTHREAD") || (getenv("FAIL_SECOND_THREAD") && thread_calls==2))return ENOMEM;return pthread_create(t,a,f,arg);}
#endif
static uint32_t last_sequence,last_crc;
#ifdef CONFIG_A733_CAMERA_PERF_TEST
static uint32_t imported_bytes,imported_crc,imported_runs;
static bool stream_owned;
#endif
static int fake_mount(const char *source,const char *target,const char *type,unsigned long flags,const void *data)
{(void)source;(void)target;(void)flags;(void)data;assert(!strcmp(type,"tmpfs"));
 assert(strncmp(target,"/data/",6)); /* Real NuttX forbids nested FAT mounts. */
 if(getenv("FAIL_MOUNT")){errno=ENODEV;return -1;}mount_owned=true;return 0;}
static int fake_umount(const char *path)
{if(getenv("FAIL_UMOUNT")){errno=EBUSY;return -1;}mount_owned=false;assert(!rmdir(path));return 0;}
static int fake_statfs(const char *path,struct statfs *s)
{(void)path;memset(s,0,sizeof(*s));s->f_type=getenv("FAIL_FSTYPE")?0:TMPFS_MAGIC;return 0;}
static int fake_open(const char *path,int flags,...)
{
  if(!strcmp(path,"/dev/a733-camera-control"))return 40001;
  if(!strcmp(path,"/dev/a733-camera-frame"))return 40002;
  if(!strcmp(path,"/dev/npu0"))return 40003;
  if(flags&O_CREAT){va_list args;va_start(args,flags);int mode=va_arg(args,int);va_end(args);return open(path,flags,mode);}
  return open(path,flags);
}
static ssize_t fake_write(int fd,const void *data,size_t bytes)
{
  if(fd!=40001)return write(fd,data,bytes);
#ifdef CONFIG_A733_CAMERA_PERF_TEST
  if(bytes==12 && !memcmp(data,"stream-start",12)){assert(!stream_owned);stream_owned=true;return bytes;}
  if(bytes==11 && !memcmp(data,"stream-stop",11))
    {assert(stream_owned);stream_owned=false;if(getenv("FAIL_STREAM_STOP")){errno=EIO;return -1;}return bytes;}
  if(bytes==11 && !memcmp(data,"stream-take",11))
    {assert(stream_owned);if(getenv("FAIL_CAPTURE")){errno=EIO;return -1;}
     if(getenv("FAIL_SECOND_CAPTURE") && captures){errno=EBADMSG;return -1;}
     ++captures;return bytes;}
  if(bytes==15 && !memcmp(data,"stream-prefetch",15)){assert(stream_owned);return bytes;}
#endif
  if(bytes==16 && !memcmp(data,"frame-experiment",16))
    {if(getenv("FAIL_CAPTURE")){errno=EIO;return -1;}++captures;}
  else assert(bytes==7 && !memcmp(data,"prepare",7));
  return bytes;
}
static ssize_t fake_read(int fd,void *data,size_t bytes)
{
  if(fd!=40002)return read(fd,data,bytes);
  if(getenv("STOP_DURING_CAPTURE"))
    {char *cmd[]={"camera","live-stop"};assert(!a733_camera_live_command(2,cmd,NULL));}
  memset(data,0,bytes);uint8_t *p=data;
  for(size_t i=0;i+1<bytes;i+=2){p[i]=(uint8_t)(captures+80);p[i+1]=0;}
  return bytes;
}
static int fake_close(int fd){if(fd>=40001 && fd<=40003){__atomic_add_fetch(&closes,1,__ATOMIC_RELAXED);return 0;}return close(fd);}
static int fake_ioctl(int fd,unsigned long cmd,...)
{
  assert(fd==40003);
  va_list args;va_start(args,cmd);struct a733_npu_run_s *r=(void *)(uintptr_t)va_arg(args,unsigned long);va_end(args);
#ifdef CONFIG_A733_CAMERA_PERF_TEST
  if(cmd==A733_NPUIOC_LIVE_PREPARE)
    {
      struct a733_npu_live_prepare_s *p=(void *)r;int in=open(p->path,O_RDONLY);assert(in>=0);
      uint8_t b[4096];ssize_t n;imported_bytes=imported_crc=imported_runs=0;
      while((n=read(in,b,sizeof(b)))>0){imported_crc=a733_vision_crc32(b,n,imported_crc);imported_bytes+=(unsigned)n;}close(in);
      p->status=0;p->bytes=imported_bytes;p->crc32=imported_crc;return 0;
    }
  if(cmd==A733_NPUIOC_LIVE_PROFILE)
    {
      struct a733_npu_live_profile_s *p=(void *)r;assert(p->version==A733_NPU_LIVE_ABI);
      p->model_bytes=imported_bytes;p->model_crc32=imported_crc;p->status=0;p->cache_hit=imported_runs>1;return 0;
    }
  if(cmd==A733_NPUIOC_LIVE_RUN)++imported_runs;
  else
#endif
    assert(cmd==A733_NPUIOC_RUN_A7PM);
#ifdef CONFIG_A733_CAMERA_PERF_TEST
  __atomic_store_n(&mock_npu_active,1,__ATOMIC_SEQ_CST);
  if(__atomic_load_n(&mock_display_active,__ATOMIC_SEQ_CST))__atomic_add_fetch(&mock_overlaps,1,__ATOMIC_SEQ_CST);
  if(getenv("SLOW_NPU"))usleep(20000);
#endif
  assert(r->flags==A733_NPU_RUN_INPUT_FILE);
  int in=open(r->input_path,O_RDONLY);assert(in>=0);uint8_t b[4096];ssize_t n;uint32_t crc=0;unsigned size=0;
  while((n=read(in,b,sizeof(b)))>0){crc=a733_vision_crc32(b,n,crc);size+=(unsigned)n;}close(in);
  r->input_crc32=crc;r->input_size=size;r->output_count=6;r->status=r->hardware_status=0;r->irq_value=0x10;
  for(unsigned i=0;i<6;i++)r->output_crc32[i]=crc^(0x10203040u*(i+1));
  r->detection_count=(crc%2)?1:0;
  r->detections[0]=(struct a733_npu_detection_s){.x1_centi=10000,.y1_centi=20000,.x2_centi=40000,.y2_centi=50000,.score_milli=910,.class_id=16};
  ++inferred;if(getenv("FAIL_CRC"))r->input_crc32^=1;
  if(getenv("FAIL_IRQ"))r->irq_value=0;
#ifdef CONFIG_A733_CAMERA_PERF_TEST
  if(getenv("FAIL_VERIFY") && cmd==A733_NPUIOC_LIVE_RUN && imported_runs==2)r->output_crc32[0]^=1;
  __atomic_store_n(&mock_npu_active,0,__ATOMIC_SEQ_CST);
#endif
  return 0;
}
int a733_display_live_begin(void){assert(!lcd_owned);if(getenv("FAIL_LCD"))return -EBUSY;lcd_owned=true;lcd_thread=pthread_self();last_sequence=last_crc=0;return 0;}
int a733_display_live_frame(const uint8_t *rgb,size_t bytes,const struct a733_live_result *r)
{
  assert(lcd_owned && pthread_equal(lcd_thread,pthread_self()) && bytes==A733_LIVE_RGB_BYTES && !a733_live_result_valid(r));
  assert(a733_vision_crc32(rgb,bytes,0)==r->input_crc);
  if(r->kind==A733_LIVE_PREVIEW){assert(r->sequence>last_sequence);last_sequence=r->sequence;last_crc=r->input_crc;}
  else assert(r->sequence==last_sequence && r->input_crc==last_crc);
  ++displayed;
  if(getenv("STOP_AFTER_FRAME")){char *cmd[]={"camera","live-stop"};assert(!a733_camera_live_command(2,cmd,NULL));}
  return 0;
}
int a733_display_live_end(void){assert(lcd_owned && pthread_equal(lcd_thread,pthread_self()));lcd_owned=false;return getenv("FAIL_DISPLAY_END")?-EIO:0;}
#ifdef CONFIG_A733_CAMERA_PERF_TEST
int a733_display_live_present(const uint8_t *rgb,size_t bytes,const struct a733_live_result *r,struct a733_live_display_profile *p)
{
  assert(lcd_owned && pthread_equal(lcd_thread,pthread_self()) && bytes==A733_LIVE_RGB_BYTES && !a733_live_result_valid(r));
  ++render_calls;
  if(getenv("FAIL_RENDER") || (getenv("FAIL_SECOND_RENDER") && render_calls==2))return -EIO;
  __atomic_store_n(&mock_display_active,1,__ATOMIC_SEQ_CST);
  if(__atomic_load_n(&mock_npu_active,__ATOMIC_SEQ_CST))__atomic_add_fetch(&mock_overlaps,1,__ATOMIC_SEQ_CST);
  assert(r->sequence>last_sequence && a733_vision_crc32(rgb,bytes,0)==r->input_crc);
  last_sequence=r->sequence;last_crc=r->input_crc;++displayed;memset(p,0,sizeof(*p));
  if(getenv("SLOW_DISPLAY"))usleep(100000);
  __atomic_store_n(&mock_display_active,0,__ATOMIC_SEQ_CST);
  if(getenv("STOP_AFTER_FRAME"))
    {char *cmd[]={"camera","live-stop"};assert(!a733_camera_live_command(2,cmd,NULL));}
  return 0;
}
#endif
#define mount fake_mount
#define umount fake_umount
#define statfs(...) fake_statfs(__VA_ARGS__)
#define open fake_open
#define read fake_read
#define write fake_write
#define close fake_close
#define ioctl fake_ioctl
#define task_create fake_task_create
#define pthread_create fake_pthread_create
#include "../board/a733-cubie-a7z/openvela-overlay/apps/system/a733camera/a733_camera_live.c"
/* Delayed task entry models independent launch without real device threads. */
static int test_task(int argc,char **argv){return pending_task(argc,argv);}
int main(int argc,char **argv)
{
  assert(argc==3);struct a733_camera_tone t={64,512,512,256,256,180};
#ifdef CONFIG_A733_CAMERA_PERF_TEST
  if(!strcmp(argv[2],"slot-snapshot"))
    {
      struct live_pipe_s p={0};assert(!pthread_mutex_init(&p.lock,NULL));
      struct pipe_slot_s *s=&p.slot[0];s->state=PIPE_READING;
      s->result=(struct a733_live_result){.sequence=1,.input_crc=123};
      struct live_frame_s f={.result=s->result};
      for(unsigned i=0;i<6;i++)f.output_crc32[i]=i+99;
      assert(!pipe_submit(&p,s,&f) && p.submitted==1 && s->state==PIPE_DRAW_READY);
      memset(&f,0,sizeof(f));
      assert(s->completed.result.sequence==1 && s->completed.result.input_crc==123);
      for(unsigned i=0;i<6;i++)assert(s->completed.output_crc32[i]==i+99);
      assert(pipe_submit(&p,s,&f)==-EPROTO);
      s->state=PIPE_READING;f.result=s->result;f.result.sequence=2;
      assert(pipe_submit(&p,s,&f)==-EPROTO);
      f.result=s->result;f.result.input_crc^=1;assert(pipe_submit(&p,s,&f)==-EPROTO);
      pthread_mutex_destroy(&p.lock);
      puts("PASS actual slot snapshot owns result/six CRCs and rejects duplicate, stale and mismatched packets");return 0;
    }
#endif
  char *cmd[]={"camera","live","1","1",argv[1]};
#ifdef CONFIG_A733_CAMERA_PERF_TEST
  if(getenv("TEST_STREAM"))cmd[1]="live-stream";
  if(getenv("TEST_PIPE"))cmd[1]="live-pipe";
  if(getenv("TEST_ASYNC"))cmd[1]="live-async";
  if(getenv("TEST_ASYNC_VERIFY"))cmd[1]="live-async-verify";
  if(getenv("TEST_LEGACY"))cmd[1]="live-legacy";
  if(getenv("TEST_VERIFY"))cmd[1]="live-verify";
#endif
  int accepted=a733_camera_live_command(5,cmd,&t);
  if(!strcmp(argv[2],"task-error")){assert(accepted==-ENOMEM && !a733_camera_live_busy() && !captures);return 0;}
  assert(!accepted);
  assert(a733_camera_live_command(5,cmd,&t)==-EBUSY);
  if(!strcmp(argv[2],"stop-before"))__atomic_store_n(&stop_requested,1,__ATOMIC_RELEASE);
  test_task(0,NULL);assert(!a733_camera_live_busy() && !lcd_owned);
  if(!strcmp(argv[2],"pass")){assert(!job.error && !job.cleanup && captures>=1 && inferred>=1 && !mount_owned);}
  else if(!strcmp(argv[2],"stop-before")){assert(!job.error && !captures);}
  else if(!strcmp(argv[2],"stop-capture")){assert(!job.error && !job.cleanup && !inferred && !mount_owned);}
  else if(!strcmp(argv[2],"thread-error")){assert(job.error==-ENOMEM && !job.cleanup && !captures && !inferred && !mount_owned);}
  else if(!strcmp(argv[2],"bad-crc") || !strcmp(argv[2],"bad-irq"))
    {
      unsigned previews=1;
#ifdef CONFIG_A733_CAMERA_PERF_TEST
      if(job.optimized)previews=0;
#endif
      assert(job.error==-EPROTO && ((getenv("TEST_PIPE") || getenv("TEST_ASYNC"))?captures>=1:captures==1) && inferred==1 && displayed==previews);
    }
  else if(!strcmp(argv[2],"verify-fail")){assert(job.error==-EBADMSG && captures==1 && inferred==3 && !displayed);}
  else if(!strcmp(argv[2],"capture-error")){assert(job.error==-EIO && !inferred && !captures);}
  else if(!strcmp(argv[2],"second-capture-error"))
    {
      assert(job.error==-EBADMSG && captures==1 && ((getenv("TEST_PIPE") || getenv("TEST_ASYNC"))?inferred<=1:inferred==1));
      if(getenv("FAIL_STREAM_STOP"))
        assert(job.cleanup==-EIO && a733_camera_live_command(5,cmd,&t)==-EIO);
      else assert(!job.cleanup);
    }
  else if(!strcmp(argv[2],"mount-error")){assert(job.error==-ENODEV && !captures);}
  else if(!strcmp(argv[2],"lcd-busy")){assert(job.error==-EBUSY && !captures && !mount_owned);}
  else if(!strcmp(argv[2],"model-missing")){assert(job.error==-ENOENT && !captures && !mount_owned);}
  else if(!strcmp(argv[2],"model-wrong")){assert(job.error==-EPROTO && !captures && !mount_owned);}
  else if(!strcmp(argv[2],"fstype-error")){assert(job.error==-EXDEV && job.cleanup==-EXDEV && !captures);}
  else if(!strcmp(argv[2],"cleanup-error")){assert(job.cleanup==-EBUSY && mount_owned && a733_camera_live_command(5,cmd,&t)==-EIO);}
  else if(!strcmp(argv[2],"render-error")){assert(job.error==-EIO && !job.cleanup && !job.frames && !displayed && inferred>=1);}
  else if(!strcmp(argv[2],"second-render-error")){assert(job.error==-EIO && !job.cleanup && job.frames==1 && displayed==1 && inferred>=2);}
  else if(!strcmp(argv[2],"display-end-error")){assert(job.cleanup==-EIO && a733_camera_live_command(5,cmd,&t)==-EIO);}
  else assert(0);
#ifdef CONFIG_A733_CAMERA_PERF_TEST
  assert(!stream_owned);
  if(getenv("TEST_VERIFY"))assert(job.verified==!getenv("FAIL_VERIFY") && job.run_calls==inferred);
  if(!strcmp(argv[2],"pass") && job.optimized)
    {if(getenv("TEST_PIPE") || getenv("TEST_ASYNC") || getenv("TEST_ASYNC_VERIFY"))assert(displayed==job.frames && captures>=displayed && captures<=displayed+2);
     else assert(displayed==captures);}
  if(getenv("TEST_ASYNC") && getenv("REQUIRE_OVERLAP"))
    {assert(mock_overlaps>0);puts("PASS NPU/display calls actually overlapped in MOCK pthread execution");}
  if(getenv("TEST_ASYNC") || getenv("TEST_ASYNC_VERIFY"))
    {assert(displayed==job.frames && job.run_calls==inferred);if(!job.error && !stopping())assert(inferred==job.inferences+(job.verified?2:0));}
#endif
  if(!strcmp(argv[2],"pass"))
    {
      unsetenv("STOP_AFTER_FRAME");
      unsigned previous=inferred;cmd[3]="0";
      assert(!a733_camera_live_command(5,cmd,&t));test_task(0,NULL);
      assert(!job.error && !job.cleanup && job.frames>0 && !job.inferences && inferred==previous);
    }
  puts("PASS actual live coordinator: RAM-model/input, frame binding, cooperative/fault exit, resources; MOCK DEVICES ONLY");
}
