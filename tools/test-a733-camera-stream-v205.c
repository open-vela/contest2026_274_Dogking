/* SPDX-License-Identifier: Apache-2.0 */
#define main previous_main
#define MSEC2TICK(ms) (ms)
#include "test-a733-camera-mode-trial.c"
#undef main
static int (*worker)(int,char **);
static unsigned target,events,scenario;
static struct file lease;
int nxmutex_lock(mutex_t *m){return nxmutex_trylock(m);}
int kthread_create(const char *name,int priority,int stack,int (*fn)(int,char **),char *const args[])
{(void)args;assert(!strcmp(name,"camera-stream") && priority==100 && stack==32768);worker=fn;return 777;}
int nxsem_init(sem_t *s,int shared,unsigned value){assert(!shared);*s=value;return 0;}
int nxsem_reset(sem_t *s,int16_t value){*s=value;return 0;}
int nxsem_post(sem_t *s){++*s;return 0;}
int nxsem_tickwait_uninterruptible(sem_t *s,unsigned timeout)
{
  assert(!control_lock && timeout && ++events<1000);
  if(scenario==1){ticks+=10001;return -ETIMEDOUT;}
  assert(frame_published && frame.paused && !frame.armed && sensor[0x100]);
  assert(control_write(NULL,"prepare",7)==-EBUSY);
  assert(control_write(NULL,"stream-take",11)==11);
  assert(!frame_open(&lease));char sample;
  assert(frame_read(&lease,&sample,1)==1 && frame_readers==1);
  assert(control_write(NULL,"stream-prefetch",15)==-EBUSY);
  if(scenario!=3)assert(!frame_close(&lease));
  if(stream.frames>=target || scenario==3)
    assert(control_write(NULL,"stream-stop",11)==-EAGAIN);
  else
    {
      frame_emitted=false;frame_pattern_bias=stream.frames+1;
      if(scenario==2 && stream.frames==3)frame_bad_guard=true;
      if(scenario==4 && stream.frames==1)frame_bad_dma=true;
      if(scenario==5 && stream.frames==1)frame_drain_on_done=true;
      assert(control_write(NULL,"stream-prefetch",15)==15);
      assert(control_write(NULL,"stream-prefetch",15)==15); /* deduplicated */
    }
  assert(*s);--*s;return 0;
}
int main(int argc,char **argv)
{
  assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f && fread(original,1,660,f)==660);fclose(f);
  for(scenario=0;scenario<6;++scenario)
    {
      fresh();memset(&stream,0,sizeof(stream));events=0;target=64;
      struct file writer={.f_oflags=O_WRONLY};assert(!control_open(&writer) && !writer.f_priv);
      assert(!control_close(&writer));
      assert(control_write(NULL,"stream-start",12)==12 && stream.active && worker);
      assert(control_write(NULL,"stream-start",12)==-EBUSY);
      assert(!worker(0,NULL) && !stream.active && !sensor[0x100]);
      if(scenario==5)
        {
          /* A real failed drain MUST retain clocks/buffer/controls, and
           * STOP must expose teardown failure, not claim a safe stop. */
          assert(frame.owned && frame.cleanup_error && stream.cleanup<0);
          assert(state.controls_owned && state.bus_quarantined && r[4] && r[5]);
          assert(control_write(NULL,"stream-stop",11)==stream.cleanup);
          assert(control_write(NULL,"stream-start",12)<0);
          continue;
        }
      restored();
      assert(!frame.owned && !receiver.owned);
      assert(!stream.cleanup && control_write(NULL,"stream-stop",11)==11);
      if(scenario==0)
        {
          assert(stream.frames==64 && capture_generation==64 && repeat_ready && frame_published && sensor_writes==155);
          frame_emitted=false;frame_pattern_bias=777;
          assert(control_write(NULL,"prepare",7)==7 && control_write(NULL,"frame-experiment",16)==16);
          assert(capture_generation==65 && repeat_ready);restored();
          frame_emitted=false;events=0;target=3;
          assert(control_write(NULL,"stream-start",12)==12 && !worker(0,NULL));
          assert(!stream.error && stream.frames==3 && capture_generation==68 && repeat_ready);restored();
        }
      if(scenario==1)assert(stream.error==-ETIMEDOUT && !frame_published && !repeat_ready);
      if(scenario==2)assert(stream.frames==3 && stream.error==-EFAULT && !frame_published && !repeat_ready);
      if(scenario==3)
        {
          assert(!stream.error && frame_readers==1 && repeat_ready);
          assert(control_write(NULL,"prepare",7)==-EBUSY);
          assert(!frame_close(&lease) && !frame_readers);
        }
      if(scenario==4)
        {
          assert(stream.frames==1 && stream.error==-EBADMSG && !repeat_ready && !frame_published);
          assert(frame.first_fault.address==0x05830254 && frame.first_fault.observed==0x00220083);
          /* Operation failure remains visible; no automatic recovery/retry. */
          assert(control_write(NULL,"stream-take",11)==-EBADMSG);
          assert(control_write(NULL,"stream-start",12)<0);
        }
    }
  puts("PASS v206 actual stream broker: 64 gated frames, restart, leases, exact 00220083 rejection, clean-error STOP vs uncertain drain retention; HOST MOCK ONLY");
  return 0;
}
