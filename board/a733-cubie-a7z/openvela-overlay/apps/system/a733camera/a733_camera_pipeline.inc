/* SPDX-License-Identifier: Apache-2.0 */
/* Application-only two-slot RGB pipeline. CSI keeps its accepted single
 * permanent guarded RAW buffer. No pointer is sent over MQ; no DMA targets
 * these RGB slots. FREE -> WRITING -> READY -> READING -> FREE under lock.
 * Async adds DRAW_READY -> DRAWING before FREE. One renderer exclusively
 * owns LVGL/SPI; main owns NPU. No release until display/accounting finishes. */
enum { PIPE_FREE,PIPE_WRITING,PIPE_READY,PIPE_READING,PIPE_DRAW_READY,PIPE_DRAWING };
struct pipe_slot_s
{
  uint8_t *rgb;
  struct a733_camera_tone_plan *plan;
  struct a733_live_result result;
  struct a733_camera_transform transform;
  uint64_t capture_us,prepare_us,tone_us,crc_us,prefetch_us,ready_us;
  unsigned state;
  struct live_frame_s completed;
};
struct live_pipe_s
{
  pthread_mutex_t lock;pthread_t thread,renderer;
  struct pipe_slot_s slot[2];struct a733_camera_image source;
  uint64_t deadline,producer_wait_us,consumer_wait_us,producer_us;
  unsigned produced,consumed,discarded,high_water;
  bool stop,done,started,lock_ready;int error,cleanup;
  bool asynchronous,render_started,render_ready,inference_done,drain;
  bool npu_active,render_active;
  unsigned submitted,rendered;
  uint64_t render_wait_us,overlap_start_us,overlap_us;
};
#include <system/a733_cpu_qualify.h>
#if defined(__aarch64__) && defined(__NuttX__)
static int cpu_get(void *ctx,uint32_t *out)
{(void)ctx;cpu_set_t mask;int r=sched_getaffinity(0,sizeof(mask),&mask);if(!r)*out=(uint32_t)mask;return r<0?-errno:0;}
static int cpu_set(void *ctx,uint32_t mask)
{(void)ctx;cpu_set_t m=(cpu_set_t)mask;return sched_setaffinity(0,sizeof(m),&m)<0?-errno:0;}
static int cpu_identity(void *ctx,uint64_t *midr,uint64_t *mpidr)
{
  (void)ctx;uint64_t after;
  __asm__ volatile("mrs %0, mpidr_el1":"=r"(*mpidr)::"memory");
  __asm__ volatile("mrs %0, midr_el1":"=r"(*midr)::"memory");
  __asm__ volatile("mrs %0, mpidr_el1":"=r"(after)::"memory");
  return after==*mpidr?0:-EAGAIN;
}
static uint64_t cpu_time(void *ctx){(void)ctx;return now_us();}
static void cpu_wait(void *ctx){(void)ctx;usleep(1000);}
static int cpu_stop(void *ctx){(void)ctx;return stopping();}
static const struct a733_cpu_ops cpu_ops={cpu_get,cpu_set,cpu_identity,cpu_time,cpu_wait,cpu_stop};
#endif
static int live_cpu_bind(const char *role,unsigned cpu)
{
#if defined(__aarch64__) && defined(__NuttX__)
  struct a733_cpu_proof p;
  a733_cpu_qualify(&cpu_ops,NULL,cpu,cpu<6?0xd05:0xd0b,&p);
  uint64_t timer;__asm__ volatile("mrs %0, cntfrq_el0":"=r"(timer)::"memory");
  int hw=!p.error && a733_raw_hw_ready();
  printf("live209 cpu role=%s requested=%u actual=%llu midr=%016llx affinity-error=%d crc=%s timer-hz=%llu cpu-hz=UNKNOWN-not-measured no-clock-writes\n",
    role,cpu,(unsigned long long)((p.mpidr>>8)&255u),(unsigned long long)p.midr,-p.error,
    hw?"arm-crc32-selftest-passed":"portable",(unsigned long long)timer);
  printf("live209 migration: role=%s before=%08lx requested=%08lx readback=%08lx confirmations=%u attempts=%u elapsed-us=%llu restored=%08lx restore-error=%d\n",
    role,(unsigned long)p.before,(unsigned long)p.requested,(unsigned long)p.readback,
    p.confirmations,p.attempts,(unsigned long long)p.elapsed,(unsigned long)p.restored,p.restore_error);
  if(!p.error && cpu>=6)
    {
      /* Local vendor cpupll DTS: 0x08870000; B PLL +0x2000,
       * B mux/divider +0x201c. This running A76 proves this CPU-clock
       * domain is powered. Read twice only; NEVER program PLL/DVFS. */
      volatile const uint32_t *pll=(const uint32_t *)(uintptr_t)0x08872000;
      volatile const uint32_t *ctl=(const uint32_t *)(uintptr_t)0x0887201c;
      volatile const uint32_t *rtc=(const uint32_t *)(uintptr_t)0x07090160;
      uint32_t a=*pll,b=*ctl,c=*rtc;
      __asm__ volatile("dmb oshld" ::: "memory");
      bool stable=a==*pll && b==*ctl && c==*rtc;
      unsigned sel=(c>>14)&3u,mux=(b>>24)&7u,n=(a>>8)&255u;
      uint64_t xo=sel==1?19200000u:sel==2?26000000u:24000000u,hz=0;
      if(stable && mux<3)hz=xo;
      if(stable && mux==3 && (a&0x98000000u)==0x98000000u)
        hz=xo*(n?n:1u)/(1u<<((b>>16)&3u));
      printf("live209 clock: pll-B=%08lx cpu-B=%08lx rtc=%08lx stable=%u register-estimate-hz=%llu physical-hz=NOT-MEASURED\n",
        (unsigned long)a,(unsigned long)b,(unsigned long)c,stable,(unsigned long long)hz);
    }
  return p.restore_error;
#else
  (void)role;(void)cpu;return 0;
#endif
}
#if defined(__aarch64__) && defined(__NuttX__)
static int live_crc_exec(unsigned cpu)
{
  /* Execute the dispatcher, not just its capability query. No device I/O. */
  uint8_t *buf=malloc(65539);if(!buf)return -ENOMEM;
  for(unsigned i=0;i<65539;i++)buf[i]=(uint8_t)(i*29u+(i>>7));
  uint32_t expected[16],changed[16];uint64_t start=now_us();
  for(unsigned i=0;i<16;i++)
    expected[i]=a733_raw_software_scan(buf+3,65536,i*12345u,&changed[i]);
  uint64_t soft=now_us()-start;int r=0;start=now_us();
  for(unsigned i=0;i<16;i++)
    {
      uint32_t n=0,crc=a733_raw_scan(buf+3,65536,i*12345u,&n);
      if(crc!=expected[i] || n!=changed[i])r=-EBADMSG;
    }
  uint64_t dispatched=now_us()-start,midr=0,mpidr=0;uint32_t mask=0;
  if(!r)r=cpu_get(NULL,&mask);
  if(!r)r=cpu_identity(NULL,&midr,&mpidr);
  if(!r && (mask!=(1u<<cpu) || ((mpidr>>8)&255u)!=cpu ||
     ((midr>>4)&4095)!=0xd0b || !a733_raw_hw_ready()))r=-ENOTSUP;
  printf("live209 crc-exec: cpu=%u bytes=65536 repeats=16 raw-crc=%08lx changed=%lu software-us=%llu dispatch-us=%llu reference-equal=%u result=%d hardware-dispatch-qualified=%u not-FPS\n",
    cpu,(unsigned long)expected[15],(unsigned long)changed[15],
    (unsigned long long)soft,(unsigned long long)dispatched,r==0,r,r==0);
  free(buf);return r;
}
#endif
static int live_cpu_probe(void)
{
#if defined(__aarch64__) && defined(__NuttX__)
  pthread_mutex_lock(&status_lock);
  if(a733_camera_live_busy() || cpu_probe_busy){pthread_mutex_unlock(&status_lock);return -EBUSY;}
  cpu_probe_busy=1;__atomic_store_n(&stop_requested,0,__ATOMIC_RELEASE);
  pthread_mutex_unlock(&status_lock);
  uint32_t old;int r=cpu_get(NULL,&old);
  if(r){pthread_mutex_lock(&status_lock);cpu_probe_busy=0;pthread_mutex_unlock(&status_lock);return r;}
  for(unsigned target=6;target<8 && !r;target++)
    {
      r=live_cpu_bind("probe",target);
      uint32_t mask=0;uint64_t midr=0,mpidr=0;
      if(!r)r=cpu_get(NULL,&mask);
      if(!r)r=cpu_identity(NULL,&midr,&mpidr);
      if(!r && (mask!=(1u<<target) || ((mpidr>>8)&255u)!=target ||
         ((midr>>4)&4095)!=0xd0b || !a733_raw_hw_ready()))r=-ENOTSUP;
      if(!r)r=live_crc_exec(target);
    }
  int restore=cpu_set(NULL,old);uint32_t back=0;
  if(!restore)restore=cpu_get(NULL,&back);
  if(!restore && back!=old)restore=-EIO;
  printf("live209 cpu-probe: result=%d restore=%d before=%08lx readback=%08lx camera-NPU-SPI-not-accessed\n",
    r,restore,(unsigned long)old,(unsigned long)back);
  pthread_mutex_lock(&status_lock);cpu_probe_busy=0;pthread_mutex_unlock(&status_lock);
  return restore?restore:r;
#else
  puts("live209 cpu-probe: unsupported architecture; NOT A733 hardware proof");return -ENOTSUP;
#endif
}
static bool pipe_halt(struct live_pipe_s *p)
{
  pthread_mutex_lock(&p->lock);bool stop=p->stop || p->error;pthread_mutex_unlock(&p->lock);
  return stop || stopping();
}
static void *pipe_producer(void *arg)
{
  struct live_pipe_s *p=arg;int r=live_cpu_bind("prepare",7);
  for(unsigned seq=1;!r && !pipe_halt(p) && now_us()<p->deadline && seq<=A733_LIVE_MAX_FRAMES;++seq)
    {
      struct pipe_slot_s *s=&p->slot[(seq-1)%2];uint64_t wait=now_us();
      for(;;)
        {
          pthread_mutex_lock(&p->lock);bool free=s->state==PIPE_FREE;
          if(free)s->state=PIPE_WRITING;
          pthread_mutex_unlock(&p->lock);
          if(free)break;
          if(pipe_halt(p) || now_us()>=p->deadline)goto done;
          if(now_us()-wait>7000000){r=-ETIMEDOUT;goto done;}
          usleep(1000);
        }
      uint64_t start=now_us();p->producer_wait_us+=start-wait;
      r=stream_control("stream-take",3000);int fd=-1;
      if(!r){fd=open("/dev/a733-camera-frame",O_RDONLY);if(fd<0)r=-errno;}
      if(fd>=0){r=read_exact(fd,(uint8_t *)p->source.data,RAW_BYTES);if(close(fd)<0 && !r)r=-errno;}
      s->capture_us=now_us()-start;
      if(r || pipe_halt(p))goto done;
      uint64_t tick=now_us();r=stream_control("stream-prefetch",3000);
      s->prefetch_us=now_us()-tick;if(r)goto done;
      tick=now_us();p->source.sequence=seq;p->source.timestamp_us=tick;
      r=a733_camera_tone_plan_render(s->plan,&p->source,&s->transform);
      s->tone_us=now_us()-tick;
      if(r || pipe_halt(p))goto done;
      s->result=(struct a733_live_result){.sequence=seq};
      uint64_t ct=now_us();s->result.input_crc=a733_vision_crc32(s->rgb,A733_LIVE_RGB_BYTES,0);
      s->crc_us=now_us()-ct;s->prepare_us=now_us()-tick;s->ready_us=now_us();
      pthread_mutex_lock(&p->lock);
      if(p->stop){pthread_mutex_unlock(&p->lock);goto done;}
      s->state=PIPE_READY;++p->produced;p->producer_us+=now_us()-start;
      unsigned occupied=0;for(unsigned i=0;i<2;++i)occupied+=p->slot[i].state!=PIPE_FREE;
      if(occupied>p->high_water)p->high_water=occupied;
      pthread_mutex_unlock(&p->lock);
    }
done:
  pthread_mutex_lock(&p->lock);if(r && !p->error)p->error=r;p->done=true;pthread_mutex_unlock(&p->lock);return NULL;
}
/* lock held: overlap means NPU-stage work (including its CPU validation)
 * and a synchronous display call overlap. It is NOT pure hardware bus time. */
static void pipe_overlap(struct live_pipe_s *p,bool npu,bool on)
{
  uint64_t t=now_us();
  if(p->npu_active && p->render_active)p->overlap_us+=t-p->overlap_start_us;
  if(npu)p->npu_active=on;else p->render_active=on;
  p->overlap_start_us=t;
}
static void pipe_mark_npu(struct live_pipe_s *p,bool on)
{pthread_mutex_lock(&p->lock);pipe_overlap(p,true,on);pthread_mutex_unlock(&p->lock);}
static void *pipe_renderer(void *arg)
{
  struct live_pipe_s *p=arg;int r=live_cpu_bind("display",5);bool owns=false;
  if(!r && !stopping()){r=a733_display_live_begin();owns=!r;}
  if(!r && !owns)r=-ECANCELED;
  pthread_mutex_lock(&p->lock);
  p->render_ready=true;if(r && !p->error)p->error=r;
  pthread_mutex_unlock(&p->lock);
  for(unsigned seq=1;!r;++seq)
    {
      struct pipe_slot_s *s=&p->slot[(seq-1)%2];uint64_t wait=now_us();
      for(;;)
        {
          pthread_mutex_lock(&p->lock);
          r=p->error;
          bool stop=(p->stop && !p->drain) || stopping() ||
                    (p->inference_done && seq>p->submitted);
          bool ready=!r && !stop && s->state==PIPE_DRAW_READY;
          if(ready)
            {
              if(s->completed.result.sequence!=seq ||
                 s->completed.result.input_crc!=s->result.input_crc)r=-ESTALE;
              else {s->state=PIPE_DRAWING;pipe_overlap(p,false,true);}
            }
          pthread_mutex_unlock(&p->lock);
          if(r || stop)goto done;
          if(ready)break;
          if(now_us()-wait>7000000){r=-ETIMEDOUT;goto done;}
          usleep(1000);
        }
      uint64_t t=now_us();p->render_wait_us+=t-wait;
      r=a733_display_live_present(s->rgb,A733_LIVE_RGB_BYTES,
                                  &s->completed.result,&s->completed.dp);
      s->completed.display_us=now_us()-t;
      pthread_mutex_lock(&p->lock);pipe_overlap(p,false,false);
      pthread_mutex_unlock(&p->lock);
      if(r)break;
      live_complete(&s->completed);
      /* Complete accounting/logging before FREE: no producer may overwrite
       * the RGB, result, six CRCs or timings while this thread uses them. */
      pthread_mutex_lock(&p->lock);
      if(s->state!=PIPE_DRAWING)r=-EPROTO;
      else {++p->rendered;++p->consumed;s->state=PIPE_FREE;}
      pthread_mutex_unlock(&p->lock);
    }
done:
  if(owns)p->cleanup=a733_display_live_end();
  pthread_mutex_lock(&p->lock);
  if(r && !p->error)p->error=r;
  pthread_mutex_unlock(&p->lock);return NULL;
}
static int pipe_start(struct live_pipe_s *p,const struct a733_camera_image *source,
                      uint8_t *rgb,struct a733_camera_tone_plan *plan,uint64_t deadline)
{
  int r=pthread_mutex_init(&p->lock,NULL);if(r)return -r;p->lock_ready=true;
  p->source=*source;p->deadline=deadline;p->slot[0].rgb=rgb;p->slot[0].plan=plan;
  p->slot[1].rgb=malloc(A733_LIVE_RGB_BYTES);
  if(!p->slot[1].rgb)return -ENOMEM;
  r=a733_camera_tone_plan_create(source,p->slot[1].rgb,A733_LIVE_RGB_BYTES,640,640,114,&job.tone,&p->slot[1].plan);
  if(r)return r;
  pthread_attr_t attr;r=pthread_attr_init(&attr);if(r)return -r;
  r=pthread_attr_setstacksize(&attr,32768);
  p->asynchronous=job.asynchronous;
  if(!r && p->asynchronous)
    {
      r=pthread_create(&p->renderer,&attr,pipe_renderer,p);
      if(!r)
        {
          p->render_started=true;uint64_t start=now_us();
          for(;;)
            {
              pthread_mutex_lock(&p->lock);bool ready=p->render_ready;int e=p->error;
              pthread_mutex_unlock(&p->lock);
              if(ready){r=e?-e:0;break;}
              if(now_us()-start>7000000){r=ETIMEDOUT;break;}
              usleep(1000);
            }
        }
    }
  if(!r)r=pthread_create(&p->thread,&attr,pipe_producer,p);
  pthread_attr_destroy(&attr);if(!r)p->started=true;
  return r?-r:0;
}
/* Return 1 for normal producer completion/deadline, negative for faults.
 * No newer result can ever be paired with an older RGB. No frame skipping. */
static int pipe_take(struct live_pipe_s *p,unsigned seq,struct pipe_slot_s **out)
{
  uint64_t wait=now_us();*out=NULL;
  for(;;)
    {
      pthread_mutex_lock(&p->lock);int error=p->error;bool done=p->done;
      struct pipe_slot_s *s=&p->slot[(seq-1)%2];
      if(!error && s->state==PIPE_READY)
        {
          if(s->result.sequence!=seq){pthread_mutex_unlock(&p->lock);return -ESTALE;}
          s->state=PIPE_READING;*out=s;p->consumer_wait_us+=now_us()-wait;
          pthread_mutex_unlock(&p->lock);return 0;
        }
      pthread_mutex_unlock(&p->lock);
      if(error)return error;
      if(done || pipe_halt(p) || now_us()>=p->deadline)return 1;
      if(now_us()-wait>7000000)return -ETIMEDOUT;
      usleep(1000);
    }
}
static void pipe_release(struct live_pipe_s *p,struct pipe_slot_s *s)
{
  pthread_mutex_lock(&p->lock);
  if(s->state!=PIPE_READING)p->error=-EPROTO;
  else {s->state=PIPE_FREE;++p->consumed;}
  pthread_mutex_unlock(&p->lock);
}
static int pipe_submit(struct live_pipe_s *p,struct pipe_slot_s *s,
                       const struct live_frame_s *f)
{
  pthread_mutex_lock(&p->lock);int r=p->error;
  if(!r && (s->state!=PIPE_READING || s->result.sequence!=f->result.sequence ||
     s->result.input_crc!=f->result.input_crc || f->result.sequence!=p->submitted+1))r=-EPROTO;
  if(!r){s->completed=*f;++p->submitted;s->state=PIPE_DRAW_READY;}
  pthread_mutex_unlock(&p->lock);return r;
}
static int pipe_finish(struct live_pipe_s *p,bool drain)
{
  if(!p->lock_ready)return 0;
  pthread_mutex_lock(&p->lock);p->stop=true;p->inference_done=true;p->drain=drain;
  pthread_mutex_unlock(&p->lock);
  if(p->started){int e=pthread_join(p->thread,NULL);if(e)return -e;p->started=false;}
  if(p->render_started){int e=pthread_join(p->renderer,NULL);if(e)return -e;p->render_started=false;}
  for(unsigned i=0;i<2;++i)if(p->slot[i].state!=PIPE_FREE)++p->discarded;
  printf("live208 pipe: slots=2 produced=%u consumed=%u discarded-at-stop=%u high-water=%u producer-us=%llu producer-wait-us=%llu consumer-wait-us=%llu error=%d same-frame-binding=1\n",
    p->produced,p->consumed,p->discarded,p->high_water,(unsigned long long)p->producer_us,
    (unsigned long long)p->producer_wait_us,(unsigned long long)p->consumer_wait_us,p->error);
  if(p->asynchronous)
    printf("live209 async: submitted=%u rendered=%u display-wait-us=%llu npu-stage/display-overlap-us=%llu drain=%u error=%d cleanup=%d sole-LVGL-owner=1 count-after-flush=1\n",
      p->submitted,p->rendered,(unsigned long long)p->render_wait_us,
      (unsigned long long)p->overlap_us,p->drain,p->error,p->cleanup);
  return 0;
}
static void pipe_destroy(struct live_pipe_s *p)
{
  a733_camera_tone_plan_destroy(p->slot[1].plan);free(p->slot[1].rgb);
  if(p->lock_ready)pthread_mutex_destroy(&p->lock);
  free(p);
}
