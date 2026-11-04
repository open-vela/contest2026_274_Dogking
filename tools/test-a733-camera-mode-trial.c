/* SPDX-License-Identifier: Apache-2.0 */
/* Real adapter + cold/mode/TWI/SHA256, simulated I2C register device only. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "csi-size-host-model.h"
#include "../board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733/a733_camera_control.c"
static uint32_t r[11],ticks,pending;
static const uint32_t addr[]={0x02002518,0x02000280,0x02000284,0x02000290,
  0x02003804,0x02002e8c,0x0251300c,0x02513010,0x02513014,0x02513018,0x02513008};
static uint8_t sensor[65536],original[660];
static unsigned accesses,phase,regnum,sensor_writes,transactions,stops;
static unsigned callbacks,fail_at;static bool failed,bad_readback,nack,arb,stop_stall;
static int idx(uintptr_t a)
{for(unsigned i=0;i<11;++i)if(addr[i]==a)return (int)i;assert(!"CSI/DMA or unlisted MMIO");return -1;}
static uint32_t rx_a[128],rx_v[128];static unsigned rx_n;
static bool frame_emitted,frame_bad_guard,frame_bad_pixel,frame_late_error;
static bool frame_bad_dma,frame_drain_stuck,frame_drain_on_done;
static unsigned frame_pattern_bias;
static unsigned rx_idx(uintptr_t a)
{
  assert((link_trial_active && a733_mipi_rx_readable(a)) ||
         (frame_trial_active && a733_csi_capture_readable(a)));
  for(unsigned i=0;i<rx_n;++i)if(rx_a[i]==a)return i;
  assert(rx_n<128);rx_a[rx_n]=a;return rx_n++;
}
uint32_t getreg32(uintptr_t a)
{
  if(a==0x02002000 || a==0x020020a0 || a==0x02003840 || a==0x02003844 ||
     a==0x02000584 || a==0x02000588 || a==0x020005a4 || a==0x020005a8 ||
     (a>=0x05800000 && a<0x05820000) ||
     (frame_trial_active && a733_csi_capture_readable(a)))
    {
      ++accesses;unsigned i=rx_idx(a);
      if(a==0x02002000)return 0xf8675f00;
      if(a==0x020020a0)return 0xff126310;
      if(a==0x020025e4)return 0x40307;
      if(a==0x03900000)return 0x16;
      if(a==0x05830008 && frame_drain_stuck)return 1;
      uint32_t automatic;
      if(frame_trial_active && csi_size_auto_read(a,rx_v[rx_idx(0x05830000)],
          rx_v[rx_idx(0x05830204)],rx_v[rx_idx(0x05830210)],rx_v[rx_idx(0x05830214)],&automatic))return automatic;
        if(a==0x05830254 && frame_emitted && !sensor[0x100] && frame_late_error)return rx_v[i]|0x40;
      if(a==0x05830254 && sensor[0x100] && !frame_emitted && frame.armed && frame.ready)
        {
          if(frame_bad_dma)return rx_v[i]|0x00220083;
          assert(frame.armed && frame.ready && rx_v[rx_idx(0x05830220)]==frame.plan.address_word);
          for(unsigned n=0;n<3840u*1080u;n+=2)
            {unsigned value=((n/2)+frame_pattern_bias)%1024;frame_buffer[64+n]=value&255;frame_buffer[65+n]=value>>8;}
          if(frame_bad_guard)frame_buffer[64+frame.plan.payload_capacity]^=1;
          if(frame_bad_pixel)frame_buffer[65]=0xff;
          rx_v[i]|=2;frame_emitted=true;
          if(frame_drain_on_done)frame_drain_stuck=true;
        }
      uint32_t moving=a733_mipi_rx_volatile_mask(a);
      if(moving)rx_v[i]=(rx_v[i]&~moving)|((accesses*65537u)&moving);
      if(a==0x05811518 && sensor[0x100])rx_v[i]|=0xf;
      if(a==0x0581151c && sensor[0x100])return 0x2b;
      return rx_v[i];
    }
  int i=idx(a);if((++accesses%32)==0)++ticks;
  if(i>=6)assert((r[5]&0x10001)==0x10001);
  if(i==9)r[i]=0;
  if(i==10)
    {
      assert(id_trial_active && regnum<65536 && (r[3]&0x300)==0x300);
      unsigned value=sensor[regnum];
      if(bad_readback && regnum==0x034e)value^=1;
      ++regnum;return value;
    }
  return r[i];
}
void putreg32(uint32_t v,uintptr_t a)
{
  if(a==0x02003840 || a==0x02003844 || a==0x02000584 || a==0x02000588 ||
     a==0x020005a4 || a==0x020005a8 || (a>=0x05800000 && a<0x05820000) ||
     (frame_trial_active && a733_csi_capture_write_mask(a)))
    {
      ++accesses;unsigned i=rx_idx(a);assert(a733_mipi_rx_write_mask(a) ||
        (frame_trial_active && a733_csi_capture_write_mask(a)));
      if(a==0x05811518 || a==0x05830254 || a==0x05821044)rx_v[i]&=~v;
      else if(frame_trial_active && csi_size_auto_ignores_write(a,rx_v[rx_idx(0x05830000)])){}
      else
        {
          uint32_t moving=a733_mipi_rx_volatile_mask(a);
          rx_v[i]=(rx_v[i]&moving)|(v&~moving);
        }
      return;
    }
  int i=idx(a);if((++accesses%32)==0)++ticks;
  assert(i!=0 && i!=7);
  if(i>=6)assert((r[5]&0x10001)==0x10001);
  if(i==10){pending=v;return;}
  if(i!=6){r[i]=v;return;}
  if(!v){r[i]=0;return;}
  if(v==0x58){++stops;r[i]=stop_stall?0x50:0x40;r[7]=0xf8;phase=0;return;}
  if(!id_trial_active || !(r[3]&0x300)){assert(v==0x48);r[i]=0x40;return;}
  r[i]=0x48;
  switch(phase)
    {
      case 0:assert(v==0x68);++transactions;r[7]=8;phase=1;break;
      case 1:assert(v==0x48 && pending==0x20);r[7]=arb?0x38:nack?0x20:0x18;phase=2;break;
      case 2:assert(v==0x48);regnum=pending<<8;r[7]=0x28;phase=3;break;
      case 3:assert(v==0x48);regnum|=pending;r[7]=0x28;phase=4;break;
      case 4:
        if(v==0x68){r[7]=0x10;phase=5;}
        else
          {
            assert(v==0x48 && regnum>=0x100 && regnum!=0x103 && regnum!=0xffff);
            assert(regnum!=0x100 || pending==0 || (link_trial_active && receiver.ready));
            sensor[regnum]=pending;++sensor_writes;r[7]=0x28;phase=7;
          }
        break;
      case 5:assert(v==0x48 && pending==0x21);r[7]=0x40;phase=6;break;
      case 6:assert(v==0x48 || v==0x4c);r[7]=v==0x4c?0x50:0x58;break;
      default:assert(!"Unexpected continuation after write value");
    }
}
uint32_t clock_systime_ticks(void){return ticks;}
int nxsig_usleep(unsigned us){ticks+=us/1000;return 0;}
int nxmutex_trylock(mutex_t *m){if(*m)return -EBUSY;*m=1;return 0;}
int nxmutex_unlock(mutex_t *m){assert(*m);*m=0;return 0;}
int register_driver(const char *p,const struct file_operations *o,mode_t m,void *v)
{(void)p;(void)o;(void)m;(void)v;assert(!accesses);return 0;}
static int fr(void *p,uint32_t a,uint32_t *v,unsigned ms)
{int ret=read32(p,a,v,ms);if(++callbacks==fail_at){failed=true;return -EIO;}return ret;}
static int fw(void *p,uint32_t a,uint32_t v,unsigned ms)
{int ret=write32(p,a,v,ms);if(++callbacks==fail_at){failed=true;return -EIO;}return ret;}
static struct a733_imx214_cold_io fault_io;
static void fresh(void)
{
  memset(r,0,sizeof(r));r[1]=r[2]=~0u;r[7]=0xf8;
  memset(sensor,0,sizeof(sensor));sensor[0x16]=2;sensor[0x17]=0x14;
  memset(&state,0,sizeof(state));memset(&captured,0,sizeof(captured));
  memset(&receiver,0,sizeof(receiver));memset(rx_a,0,sizeof(rx_a));memset(rx_v,0,sizeof(rx_v));rx_n=0;
  memset(&frame,0,sizeof(frame));frame_requested=frame_trial_active=frame_published=false;
  frame_emitted=frame_bad_guard=frame_bad_pixel=frame_late_error=false;frame_validation=0;
  frame_bad_dma=frame_drain_stuck=frame_drain_on_done=false;
  frame_pattern_bias=0;
  link_trial_active=link_attempted=stream_on_attempted=false;stream_error=stop_error=0;
  consumed=mode_consumed=id_trial_active=mode_trial_active=false;
  frame_readers=capture_generation=0;repeat_ready=repeat_cycle=false;
  mode_blob_error=command_error=control_lock=0;
  accesses=phase=regnum=sensor_writes=transactions=stops=ticks=0;
  callbacks=fail_at=0;failed=bad_readback=nack=arb=stop_stall=false;
  g_a733_camera_power_handoff=(struct a733_camera_power_handoff){
    .signature=A733_CAMERA_POWER_SIGNATURE,.abi=A733_CAMERA_POWER_ABI,
    .ready=A733_CAMERA_POWER_READY,.values={3,0,0xff,0x17,0xc1,0x0d}};
  g_a733_camera_mode_handoff=(struct a733_camera_mode_handoff){
    .signature=A733_CAMERA_MODE_SIGNATURE,.abi=1,.bytes=660,.ready=A733_CAMERA_MODE_READY};
  for(unsigned i=0;i<660;++i)g_a733_camera_mode_handoff.data[i]=original[i];
  fault_io=io;fault_io.read32=fr;fault_io.write32=fw;
}
static void prepare(void){assert(control_write(NULL,"prepare",7)==7);assert(!transactions);}
static void restored(void)
{assert(!state.controls_owned && !state.cleanup_error && !r[4] && !r[5] && !(r[3]&0x300));}
static int fault_trial(void)
{
  id_trial_active=mode_trial_active=true;
  int ret=a733_imx214_cold_trial_mode(&state,&fault_io,original,sizeof(original));
  id_trial_active=mode_trial_active=false;return ret;
}
int main(int argc,char **argv)
{
  assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
  assert(fread(original,1,sizeof(original),f)==sizeof(original) && fgetc(f)==EOF);assert(!fclose(f));
  unsigned cases=0;
  for(unsigned i=0;i<660;++i)
    {
      fresh();g_a733_camera_mode_handoff.data[i]^=1;
      assert(capture_mode()==-EBADMSG && !accesses);++cases;
    }
  for(unsigned i=0;i<5;++i)
    {
      fresh();
      if(i==0)g_a733_camera_mode_handoff.signature=0;
      if(i==1)g_a733_camera_mode_handoff.abi=0;
      if(i==2)g_a733_camera_mode_handoff.bytes=661;
      if(i==3)g_a733_camera_mode_handoff.ready=0;
      if(i==4)g_a733_camera_mode_handoff.reserved[27]=1;
      assert(capture_mode()<0 && !accesses);++cases;
    }
  fresh();assert(control_write(NULL,"mode-experiment",15)==-EACCES && !accesses && !mode_consumed);++cases;
  fresh();prepare();assert(control_write(NULL,"mode-experiment",15)==15);
  assert(state.id_trial.identity_match && state.mode_trial_attempted && !state.mode_trial.error);
  assert(sensor_writes==153 && state.mode_trial.writes==153 && state.mode_trial.verified_registers==14);
  assert(!sensor[0x100] && transactions==171 && stops==171);restored();++cases;
  unsigned before=accesses;assert(control_write(NULL,"mode-experiment",15)==-EACCES && accesses==before);
  fresh();prepare();g_a733_camera_mode_handoff.data[659]^=1;before=accesses;
  assert(control_write(NULL,"mode-experiment",15)==-EBADMSG && accesses==before && !transactions);
  assert(control_write(NULL,"park",4)==4);restored();++cases;
  for(unsigned variant=0;variant<5;++variant)
    {
      fresh();prepare();
      if(variant==0)sensor[0x17]=0x15;
      if(variant==1)bad_readback=true;
      if(variant==2)nack=true;
      if(variant==3)arb=true;
      if(variant==4)stop_stall=true;
      assert(control_write(NULL,"mode-experiment",15)<0 && !sensor[0x100]);
      if(variant<3)restored();else assert(state.bus_quarantined && state.controls_owned && !(r[3]&0x300));
      ++cases;
    }
  fresh();prepare();assert(!fault_trial());unsigned total=callbacks;
  for(unsigned i=1;i<=total;++i)
    {
      fresh();prepare();fail_at=i;
      assert(fault_trial()<0 && failed && !sensor[0x100]);
      assert(state.state==A733_IMX214_COLD_FAULT);
      assert(state.controls_owned || !state.configured_mclk_hz);
      if(!state.cleanup_error)assert(!(r[3]&0x300));
      ++cases;
    }
  printf("PASS: %u v194 composed mode cases (all %u callback failures, every artifact byte, real SHA256/TWI/153 writes/14 readbacks); SYNTHETIC ONLY, NO CSI/DMA\n",cases,total);
  return 0;
}
