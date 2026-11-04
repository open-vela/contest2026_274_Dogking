/* SPDX-License-Identifier: Apache-2.0 */
/* Actual v193 adapter/core/transport, synthetic hardware only. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733/a733_camera_control.c"

static const uint32_t addresses[]={0x02002518,0x02000280,0x02000284,
  0x02000290,0x02003804,0x02002e8c,0x0251300c,0x02513010,
  0x02513014,0x02513018,0x02513008};
static uint32_t regs[11],ticks,pending;
static unsigned accesses,writes,starts,stops,position,phase,release_count;
static bool nack,arb,wrong_id,stall,stop_stall,frozen;
static unsigned trial_ops,fail_at,fail_write_at,trial_writes;
static bool fail_once;

static int index_of(uintptr_t a)
{
  for(unsigned i=0;i<11;++i) if(a==addresses[i]) return (int)i;
  assert(!"Unlisted MMIO/CSI/ISP/DMA access");return -1;
}
uint32_t getreg32(uintptr_t a)
{
  int i=index_of(a);++accesses;if(!frozen)++ticks;
  if(i>=6)assert((regs[5]&0x10001)==0x10001);
  if(i==9)regs[i]=0;
  if(i==6 && stall && starts)return 0x40;
  if(i==10)
    {
      assert(id_trial_active && starts==2 && position<2 && (regs[3]&0x300)==0x300);
      return position++ ? (wrong_id?0x15:0x14) : 2;
    }
  return regs[i];
}
void putreg32(uint32_t v,uintptr_t a)
{
  int i=index_of(a);++accesses;++writes;if(!frozen)++ticks;
  assert(i!=0 && i!=7);
  if(i>=6)assert((regs[5]&0x10001)==0x10001);
  if(i==3 && (v&0x300)==0x300)++release_count;
  if(i==10)
    {assert(id_trial_active);pending=v;regs[i]=v;return;}
  if(i!=6){regs[i]=v;return;}
  if(v==0){regs[i]=0;return;}
  if(v==0x58)
    {++stops;regs[i]=stop_stall?0x50:0x40;regs[7]=0xf8;return;}
  if(!id_trial_active || !(regs[3]&0x300))
    {assert(v==0x48);regs[i]=0x40;return;}
  assert((regs[3]&0x300)==0x300);
  regs[i]=0x48;
  switch(phase++)
    {
      case 0:assert(v==0x68);++starts;regs[7]=8;break;
      case 1:assert(v==0x48 && pending==0x20);regs[7]=arb?0x38:nack?0x20:0x18;break;
      case 2:assert(v==0x48 && !pending);regs[7]=0x28;break;
      case 3:assert(v==0x48 && pending==0x16);regs[7]=0x28;break;
      case 4:assert(v==0x68);++starts;regs[7]=0x10;break;
      case 5:assert(v==0x48 && pending==0x21);regs[7]=0x40;break;
      case 6:assert(v==0x4c);regs[7]=0x50;break;
      case 7:assert(v==0x48);regs[7]=0x58;break;
      default:assert(!"Second ID transaction or unexpected register write");
    }
}
uint32_t clock_systime_ticks(void){return ticks;}
int nxsig_usleep(unsigned us)
{assert(us==1000 || us==10000 || us==30000);if(!frozen)ticks+=us/1000;return 0;}
int nxmutex_trylock(mutex_t *m){if(*m)return -EBUSY;*m=1;return 0;}
int nxmutex_unlock(mutex_t *m){assert(*m);*m=0;return 0;}
int register_driver(const char *p,const struct file_operations *o,mode_t m,void *v)
{(void)p;(void)o;(void)m;(void)v;assert(!accesses);return 0;}

/* Fault injection around the real adapter, including callbacks which fail
 * AFTER the write took effect. No synthetic successful MMIO substitution.
 */
static int injected_read(void *p,uint32_t a,uint32_t *v,unsigned ms)
{
  int ret=read32(p,a,v,ms);
  if(++trial_ops==fail_at && !fail_once){fail_once=true;return -EIO;}
  return ret;
}
static int injected_write(void *p,uint32_t a,uint32_t v,unsigned ms)
{
  int ret=write32(p,a,v,ms);
  if(++trial_writes==fail_write_at && !fail_once){fail_once=true;return -EIO;}
  if(++trial_ops==fail_at && !fail_once){fail_once=true;return -EIO;}
  return ret;
}
static struct a733_imx214_cold_io injected_io;
static void fresh(void)
{
  memset(regs,0,sizeof(regs));regs[1]=regs[2]=0xffffffff;regs[7]=0xf8;
  memset(&state,0,sizeof(state));memset(&captured,0,sizeof(captured));
  consumed=id_trial_active=false;control_lock=command_error=0;
  accesses=writes=starts=stops=position=phase=release_count=0;ticks=pending=0;
  nack=arb=wrong_id=stall=stop_stall=frozen=false;
  trial_ops=fail_at=fail_write_at=trial_writes=0;fail_once=false;
  g_a733_camera_power_handoff=(struct a733_camera_power_handoff){
    .signature=A733_CAMERA_POWER_SIGNATURE,.abi=A733_CAMERA_POWER_ABI,
    .ready=A733_CAMERA_POWER_READY,.values={3,0,0xff,0x17,0xc1,0x0d}};
  assert(!a733_camera_control_initialize());
  injected_io=io;injected_io.read32=injected_read;injected_io.write32=injected_write;
}
static void prepare(void)
{assert(control_write(NULL,"prepare",7)==7);assert(!release_count && !starts);}
static void restored(void)
{
  assert(!state.controls_owned && !state.configured_mclk_hz);
  assert(regs[1]==0xffffffff && regs[2]==0xffffffff && !(regs[3]&0x300));
  assert(!regs[4] && !regs[5]);
}
static int injected_trial(void)
{
  id_trial_active=true;
  int ret=a733_imx214_cold_trial_id(&state,&injected_io);
  id_trial_active=false;
  return ret;
}
int main(void)
{
  unsigned cases=0;
  fresh();unsigned before=accesses;
  assert(control_write(NULL,"id-experiment",13)==-EACCES && accesses==before);++cases;
  fresh();prepare();
  assert(control_write(NULL,"calibrate",9)==9);before=accesses;
  assert(control_write(NULL,"id-experiment",13)==-EACCES && accesses==before);++cases;
  fresh();prepare();control_lock=1;before=accesses;
  assert(control_write(NULL,"id-experiment",13)==-EBUSY && accesses==before);++cases;
  fresh();prepare();
  assert(control_write(NULL,"id-experiment",13)==13);
  assert(state.id_trial_attempted && state.id_trial.identity_match && state.id_trial.chip_id==0x214);
  assert(!state.id_trial.snapshot_complete && state.id_trial.reads==1);
  assert(starts==2 && stops==1 && position==2 && release_count==1 && !id_trial_active);
  restored();before=accesses;
  assert(control_write(NULL,"id-experiment",13)==-EALREADY && accesses==before);++cases;
  for(unsigned variant=0;variant<6;++variant)
    {
      fresh();prepare();
      if(variant==0)wrong_id=true;
      if(variant==1)nack=true;
      if(variant==2)arb=true;
      if(variant==3)stall=true;
      if(variant==4)stop_stall=true;
      if(variant==5){regs[6]=0x60;regs[7]=8;}
      assert(control_write(NULL,"id-experiment",13)<0);
      if(variant<2){restored();assert(stops==1 && !state.bus_quarantined);}
      else
        {
          assert(state.bus_quarantined && state.controls_owned && !(regs[3]&0x300));
          before=accesses;assert(control_write(NULL,"park",4)==-EIO && before==accesses);
          assert(stops==(unsigned)(variant==3 || variant==4));
        }
      before=accesses;
      assert(control_write(NULL,"id-experiment",13)==-EALREADY && before==accesses);
      ++cases;
    }
  /* Test the actual composed core with failure at every normal callback. */
  fresh();prepare();assert(!injected_trial());
  unsigned count=trial_ops;
  for(unsigned at=1;at<=count;++at)
    {
      fresh();prepare();fail_at=at;
      assert(injected_trial()<0 && fail_once);
      assert(state.id_trial_attempted && state.state==A733_IMX214_COLD_FAULT);
      assert(state.controls_owned || !state.configured_mclk_hz);
      if(!state.cleanup_error)assert(!(regs[3]&0x300));
      before=accesses;
      assert(a733_imx214_cold_trial_id(&state,&injected_io)==-EALREADY && accesses==before);
      ++cases;
    }
  /* Frozen timer is bounded by the transport's finite poll count; no delay
   * completion is fabricated, so a frozen settle clock fails before START.
   */
  fresh();prepare();frozen=true;
  assert(control_write(NULL,"id-experiment",13)==-ETIMEDOUT && !starts);restored();++cases;
  printf("PASS: %u v193 composed ID experiment cases; real adapter/core/TWI, fixed ID only, no strict lease, one-shot/STOP/park/quarantine and each callback failure; SYNTHETIC ONLY\n",cases);
  return 0;
}
