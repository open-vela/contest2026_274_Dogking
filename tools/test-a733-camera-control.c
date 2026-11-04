/* SPDX-License-Identifier: Apache-2.0 */
/* Actual control fops under mocked hardware. No physical PMIC/MMIO. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733/a733_camera_control.c"

static uint32_t regs[10],ticks;
static unsigned accesses;
static const uint32_t addresses[]={0x02002518,0x02000280,0x02000284,
  0x02000290,0x02003804,0x02002e8c,0x0251300c,0x02513010,0x02513014,0x02513018};
static int slot(uintptr_t a)
{
  for(unsigned i=0;i<10;++i)if(a==addresses[i])return (int)i;
  assert(!"Unexpected MMIO or sensor/DMA access");return -1;
}
uint32_t getreg32(uintptr_t a)
{
  int i=slot(a);++accesses;++ticks;
  if(i>=6)assert((regs[5]&0x10001)==0x10001);
  if(i==9)regs[i]=0;
  return regs[i];
}
void putreg32(uint32_t v,uintptr_t a)
{
  int i=slot(a);++accesses;++ticks;
  assert(i!=0 && i!=7);
  if(i>=6)assert((regs[5]&0x10001)==0x10001);
  if(i==6)assert(v==0 || v==0x48);
  regs[i]=i==6 ? v&~8u : v;
}
uint32_t clock_systime_ticks(void){return ticks;}
int nxsig_usleep(unsigned us){assert(us==1000 || us==10000);ticks+=us/1000;return 0;}
int nxmutex_trylock(mutex_t *m){if(*m)return -EBUSY;*m=1;return 0;}
int nxmutex_unlock(mutex_t *m){assert(*m);*m=0;return 0;}
int register_driver(const char *p,const struct file_operations *o,mode_t mode,void *priv)
{
  assert(((!strcmp(p,"/dev/a733-camera-control") && o==&control_ops && mode==0660) ||
    (!strcmp(p,"/dev/a733-camera-frame") && o==&frame_ops && mode==0440)) && !priv);
  assert(!accesses && !consumed);return 0;
}
static void fresh(bool proof)
{
  memset(&state,0,sizeof(state));memset(&captured,0,sizeof(captured));
  consumed=false;command_error=0;control_lock=0;ticks=accesses=0;
  memset(regs,0,sizeof(regs));regs[1]=regs[2]=0xffffffff;regs[7]=0xf8;
  struct a733_camera_power_handoff r={.signature=A733_CAMERA_POWER_SIGNATURE,
    .abi=A733_CAMERA_POWER_ABI,.ready=proof?A733_CAMERA_POWER_READY:0,
    .values={3,0,0xff,0x17,0xc1,0x0d}};
  g_a733_camera_power_handoff=r;
}
static void report(const char *needle)
{
  struct file f={0};assert(control_open(&f)==0);
  char text[REPORT_BYTES];ssize_t n=control_read(&f,text,sizeof(text)-1);
  assert(n>0);text[n]=0;assert(strstr(text,needle));
  assert(control_read(&f,text,1)==0);assert(control_close(&f)==0);
}
int main(void)
{
  fresh(false);assert(a733_camera_control_initialize()==0 && !accesses);
  report("camera-control-v203:");assert(!accesses && !consumed);
  struct file first={0};assert(control_open(&first)==0);
  struct file duplicate={.f_priv=first.f_priv};assert(control_open(&duplicate)==0);
  assert(duplicate.f_priv!=first.f_priv);assert(control_close(&first)==0);
  char byte;assert(control_read(&duplicate,&byte,1)==1);
  assert(control_close(&duplicate)==0 && !accesses && !consumed);
  assert(control_write(NULL,"prepare",7)==-ENODATA && !accesses && consumed);
  assert(control_write(NULL,"prepare",7)==-EALREADY && !accesses);
  assert(control_write(NULL,"park",4)==4 && !accesses);
  assert(control_write(NULL,"init",4)==-EINVAL && !accesses);
  fresh(true);control_lock=1;
  struct file f={0};assert(control_open(&f)==-EBUSY && !f.f_priv);
  assert(control_write(NULL,"prepare",7)==-EBUSY && !consumed && !accesses);
  control_lock=0;
  assert(control_write(NULL,"prepare\n",8)==8);
  assert(consumed && !g_a733_camera_power_handoff.ready && state.controls_owned);
  assert(state.firmware_power_assumed && state.configured_mclk_hz==24000000);
  assert(!(regs[3]&0x300));report("measured=UNKNOWN");
  unsigned before=accesses;
  assert(control_write(NULL,"prepare",7)==-EBUSY && before==accesses);
  assert(control_write(NULL,"calibrate",9)==9 && regs[4]==0x80000017);
  assert(state.configured_mclk_hz==1000000);report("physical-voltage=NOT-MEASURED");
  assert(control_write(NULL,"normal-clock",12)==12 && regs[4]==0x80000000);
  assert(control_write(NULL,"park",4)==4 && !state.controls_owned);
  assert(!regs[4] && !regs[5] && regs[1]==0xffffffff && regs[2]==0xffffffff);
  before=accesses;
  assert(control_write(NULL,"prepare",7)==-EALREADY && before==accesses);
  for(unsigned variant=0;variant<4;++variant)
    {
      fresh(true);
      if(variant==0)g_a733_camera_power_handoff.signature^=1;
      if(variant==1)g_a733_camera_power_handoff.abi^=1;
      if(variant==2)g_a733_camera_power_handoff.reserved[0]=1;
      if(variant==3)g_a733_camera_power_handoff.values[5]=0;
      assert(control_write(NULL,"prepare",7)<0 && !accesses && consumed);
    }
  puts("PASS: actual camera-control fops; normal boot ZERO MMIO, one-shot image handoff, invalid data/commands/lock, reset-held 24M/1M/park; MOCK hardware only");
  return 0;
}
