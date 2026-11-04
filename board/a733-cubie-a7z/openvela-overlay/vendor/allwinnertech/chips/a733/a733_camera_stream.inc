/* SPDX-License-Identifier: Apache-2.0 */
/* Private broker extension. Consumer sees only completed, DMA-paused frames.
 * MCSIB/sensor stay on; parser uses SCAP=1, not continuous DDR writes. The
 * static guarded buffer is NEVER freed even after uncertain drain/restore.
 * Producer owns the cold-trial stack until OFF/restore, avoiding dangling
 * bus/context pointers. control_lock is held around all device accesses. */
#include <nuttx/kthread.h>
#include <nuttx/semaphore.h>
static struct
{
  bool active,stop,pending,working,sem_ready;
  unsigned frames,delivered;
  uint32_t started,last_request;
  int error,cleanup;
  sem_t request;
} stream;

/* Called with control_lock held; returns with it held. Waiting never holds
 * that lock, so consumer read/close/status/stop cannot deadlock the producer. */
static int stream_wait_request(void)
{
  for(;;)
    {
      uint32_t now=millis(NULL);
      if(stream.stop)return 1;
      if(now-stream.started>=3602000u)return 1;
      if(now-stream.last_request>=10000u)return -ETIMEDOUT;
      if(stream.pending){stream.pending=false;stream.working=true;return 0;}
      nxmutex_unlock(&control_lock);
      int r=nxsem_tickwait_uninterruptible(&stream.request,MSEC2TICK(100));
      int lock=nxmutex_lock(&control_lock);if(lock)return lock;
      if(r && r!=-ETIMEDOUT)return r;
    }
}
static int stream_trial(void *unused,struct a733_imx214_twi3 *bus)
{
  (void)unused;link_attempted=true;uint8_t v[2];
  int r=a733_imx214_twi3_trial_read(bus,0x0340,v,2,30);
  if(!r)sensor_lines=(uint16_t)v[0]<<8|v[1];
  if(!r)r=a733_imx214_twi3_trial_read(bus,0x0202,v,2,30);
  if(!r)sensor_exposure=(uint16_t)v[0]<<8|v[1];
  if(!r)r=a733_imx214_twi3_trial_read(bus,0x0204,v,2,30);
  if(!r)sensor_gain=(uint16_t)v[0]<<8|v[1];
  if(!r)r=a733_mipi_rx_prepare(&receiver,&io);
  uintptr_t physical=(uintptr_t)frame_buffer;
  if(!r && (physical<0x40200000 || physical>0x48000000-sizeof(frame_buffer)))r=-ERANGE;
  if(!r)
    {
      memset(frame_buffer,0xa5,sizeof(frame_buffer));memset(frame_buffer+64,0xff,3840u*1088u);
      up_clean_dcache(physical,physical+sizeof(frame_buffer));
      r=a733_csi_capture_prepare(&frame,&io,physical,sizeof(frame_buffer));
    }
  if(!r){stream_on_attempted=true;r=stream_error=a733_imx214_twi3_packet_stream(bus,frame_live_check,&receiver,true);}
  while(!r)
    {
      r=a733_csi_capture_wait(&frame);
      if(!r)r=a733_csi_capture_pause(&frame);
      receiver.started=millis(NULL);receiver.polls=0; /* fresh bounded snapshot, NOT error clearing */
      if(!r)r=a733_mipi_rx_snapshot(&receiver);
      if(!r)
        {
          up_invalidate_dcache(physical,physical+sizeof(frame_buffer));
          r=frame_validation=validate_frame();uint32_t first=frame_fingerprint;
          if(!r)r=pause_ms(NULL,10);
          up_invalidate_dcache(physical,physical+sizeof(frame_buffer));
          if(!r)r=frame_validation=validate_frame();
          if(!r && first!=frame_fingerprint)r=frame_validation=-ESTALE;
        }
      if(r)break;
      frame_published=true;++capture_generation;++stream.frames;stream.working=false;
      int request=stream_wait_request();
      if(request){if(request<0)r=request;break;}
      if(capture_generation>=CAMERA_CYCLE_LIMIT){r=-EOVERFLOW;break;}
      if(__atomic_load_n(&frame_readers,__ATOMIC_ACQUIRE)){r=-EBUSY;break;}
      frame_published=false;
      memset(frame_buffer,0xa5,sizeof(frame_buffer));memset(frame_buffer+64,0xff,3840u*1088u);
      up_clean_dcache(physical,physical+sizeof(frame_buffer));
      receiver.started=millis(NULL);receiver.polls=0;
      r=a733_mipi_rx_check(&receiver);
      if(!r)r=a733_csi_capture_rearm(&frame);
    }
  /* Same source-OFF, drain/late-error, CSI/receiver restore and outer control
   * park ordering as the accepted single-shot path. No reset/retry here. */
  if(bus->packet_stream_attempted && !bus->quarantined)
    stop_error=a733_imx214_twi3_packet_stream(bus,NULL,NULL,false);
  else if(bus->packet_stream_attempted)stop_error=-EIO;
  if(!r)r=stop_error;
  if(frame.attempted){int c=a733_csi_capture_finish(&frame);if(!r)r=c;if(!r)r=frame.error;}
  /* Route uncertain downstream ownership through the outer retention branch:
   * assert source controls only, NEVER gate clocks under an undrained DMA. */
  if(frame.owned){bus->quarantined=true;return r?r:-EIO;}
  int c=a733_mipi_rx_finish(&receiver);if(!r)r=c;
  if(receiver.owned)bus->quarantined=true;
  return r;
}
static int stream_cleanup_status(void)
{
  if(frame.cleanup_error)return frame.cleanup_error;
  if(receiver.cleanup_error)return receiver.cleanup_error;
  if(state.cleanup_error)return state.cleanup_error;
  if(stop_error)return stop_error;
  if(frame.owned || receiver.owned || state.controls_owned || state.bus_quarantined)return -EIO;
  return 0;
}
static int stream_worker(int argc,char **argv)
{
  (void)argc;(void)argv;
  /* A kernel task inherits its creator's single CPU6 mask. The control broker
   * does not execute optional ISA kernels and must not share that accidental
   * restriction with infer/display. Change this task only, never the system. */
  uint32_t readback=0xff;int affinity=0;
#ifdef __NuttX__
  cpu_set_t allowed=(cpu_set_t)0xff;
  affinity=sched_setaffinity(0,sizeof(allowed),&allowed)<0?-errno:0;
  if(!affinity && sched_getaffinity(0,sizeof(readback),&readback)<0)affinity=-errno;
  if(!affinity && readback!=allowed)affinity=-EIO;
#endif
  int r=nxmutex_lock(&control_lock);if(r)return r;
  printf("camera-stream-v212: broker-mask=%08lx status=%d; no-global-policy-change\n",
    (unsigned long)readback,affinity);
  struct a733_imx214_cold_io operation=io;operation.packet_trial=stream_trial;
  r=affinity?affinity:a733_imx214_cold_trial_session(&state,&operation,mode_bytes,sizeof(mode_bytes),3605000);
  if(affinity && state.controls_owned) {
    int parked=a733_imx214_cold_park(&state,&io);if(parked)r=parked;
  }
  frame_published=stream.frames && !r && frame.completed && !frame.owned && !receiver.owned &&
    !state.controls_owned && !state.cleanup_error && !frame_validation;
  repeat_ready=frame_published;repeat_cycle=false;
  id_trial_active=mode_trial_active=link_trial_active=frame_trial_active=false;
  stream.cleanup=stream_cleanup_status();
  stream.error=r;stream.working=stream.pending=false;stream.active=false;command_error=r;
  nxmutex_unlock(&control_lock);return 0;
}
static int stream_command(const char *text,size_t n)
{
  if(n==12 && !memcmp(text,"stream-start",12))
    {
      if(stream.active)return -EBUSY;
      int r=state.controls_owned?-EBUSY:capture_handoff();
      if(!r)r=a733_imx214_cold_prepare(&state,&io);
      if(!r)r=capture_mode();
      if(r)
        {
          if(state.controls_owned && !frame.owned && !receiver.owned)
            {int c=a733_imx214_cold_park(&state,&io);if(c)r=c;}
          stream.error=r;return r;
        }
      if(!stream.sem_ready){r=nxsem_init(&stream.request,0,0);if(!r)stream.sem_ready=true;}
      else r=nxsem_reset(&stream.request,0);
      if(r){int c=a733_imx214_cold_park(&state,&io);return c?c:r;}
      stream.stop=stream.pending=false;stream.working=stream.active=true;
      stream.frames=0;stream.delivered=capture_generation;stream.error=stream.cleanup=0;
      stream.started=stream.last_request=millis(NULL);
      repeat_ready=frame_published=false;
      id_trial_active=mode_trial_active=link_trial_active=frame_trial_active=frame_requested=true;
      int pid=kthread_create("camera-stream",100,32768,stream_worker,NULL);
      if(pid<0)
        {
          r=errno?-errno:-EIO;stream.active=stream.working=false;
          id_trial_active=mode_trial_active=link_trial_active=frame_trial_active=false;
          int c=a733_imx214_cold_park(&state,&io);stream.error=c?c:r;return stream.error;
        }
      return 0;
    }
  if(n==11 && !memcmp(text,"stream-stop",11))
    {
      /* STOP reports teardown only. TAKE and status retain the original
       * operation failure. A failed capture still requires reboot; no retry
       * or clearing of cold/frame faults is introduced here. */
      if(!stream.active)return stream.cleanup;
      if(!stream.stop){stream.stop=true;int r=nxsem_post(&stream.request);if(r)return r;}
      return -EAGAIN;
    }
  bool take=n==11 && !memcmp(text,"stream-take",11);
  bool prefetch=n==15 && !memcmp(text,"stream-prefetch",15);
  if(!take && !prefetch)return -EINVAL;
  if(!stream.active)return stream.error?stream.error:-ENODATA;
  if(stream.stop)return -ECANCELED;
  stream.last_request=millis(NULL);
  if(take && frame_published && stream.delivered<capture_generation)
    {stream.delivered=capture_generation;return 0;}
  if(stream.pending || stream.working)return take?-EAGAIN:0;
  if(stream.delivered<capture_generation)return -EALREADY;
  if(__atomic_load_n(&frame_readers,__ATOMIC_ACQUIRE))return -EBUSY;
  stream.pending=true;frame_published=false;
  int r=nxsem_post(&stream.request);return r?r:take?-EAGAIN:0;
}
