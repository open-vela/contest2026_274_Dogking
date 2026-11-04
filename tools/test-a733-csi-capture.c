/* SPDX-License-Identifier: Apache-2.0 */
#include "a733_csi_capture.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "csi-size-host-model.h"
static uint32_t addresses[80],values[80],ticks;
static unsigned used,calls,fail_at,seen;
static unsigned delayed_pause,delayed_access;
static bool failed,emit,error_packet,stale,freeze,translation,drain_stuck,late_error;
static unsigned slot(uint32_t a)
{
  assert(a733_csi_capture_readable(a));
  for(unsigned i=0;i<used;++i)if(addresses[i]==a)return i;
  assert(used<80);addresses[used]=a;return used++;
}
static int fault(void){++calls;if(calls==fail_at){failed=true;return -EIO;}return 0;}
static int rd(void *p,uint32_t a,uint32_t *v,unsigned ms)
{
  (void)p;(void)ms;unsigned i=slot(a);
  if(a==0x020025e4)*v=0x40307;
  else if(a==0x03900000)*v=0x16;
  else if(a==0x03900014)*v=translation?1:0;
  else if(a==0x03900604)*v=0;
  else if(a==0x05830254)*v=values[i]|(stale?2:0)|(late_error?0x40:0)|(emit && ++seen>1?(error_packet?0x46:2):0);
  else if(a==0x05830008)*v=drain_stuck?1:0;
  else if(csi_size_auto_read(a,values[slot(0x05830000)],values[slot(0x05830204)],
                            values[slot(0x05830210)],values[slot(0x05830214)],v)){}
  else *v=values[i];
  ticks+=delayed_access;delayed_access=0;
  return fault();
}
static int wr(void *p,uint32_t a,uint32_t v,unsigned ms)
{
  (void)p;(void)ms;unsigned i=slot(a);uint32_t mask=a733_csi_capture_write_mask(a);
  assert(mask && ((values[i]^v)&~mask)==0);
  /* Independent address-space safety: no IOMMU/ISP/PLL/table/fabric writes. */
  assert(a<0x03900000 || (a>=0x05800000 && a<0x05840000));
  assert(a!=0x020025e4);
  if(a==0x05830254 || a==0x05821044)values[i]&=~v;
  else if(!csi_size_auto_ignores_write(a,values[slot(0x05830000)]))values[i]=v;
  return fault(); /* also inject after a write has taken effect */
}
static uint32_t now(void *p){(void)p;return ticks;}
static int delay(void *p,unsigned ms){(void)p;if(!freeze)ticks+=ms+delayed_pause;delayed_pause=0;return fault();}
static const struct a733_imx214_cold_io io={.read32=rd,.write32=wr,.now_ms=now,.delay_ms=delay};
static void fresh(void)
{
  used=calls=fail_at=ticks=seen=0;
  delayed_pause=delayed_access=0;
  failed=emit=error_packet=stale=freeze=translation=drain_stuck=late_error=false;
  memset(addresses,0,sizeof(addresses));memset(values,0,sizeof(values));
  values[slot(0x05800004)]=0x100;values[slot(0x05800800)]=1;
  values[slot(0x05830210)]=1280u<<16;values[slot(0x05830214)]=720u<<16;
}
int main(int argc,char **argv)
{
  unsigned cases=0;fresh();struct a733_csi_capture s={0};
  if(argc==2 && !strcmp(argv[1],"legacy"))
    {
      assert(a733_csi_capture_prepare(&s,&io,0x41000000,4*1024*1024)==-EIO && !s.armed);
      assert(s.first_fault.address==0x05830238 && s.first_fault.expected==0x0f000f00 && s.first_fault.observed==0x07800780);
      assert(a733_csi_capture_finish(&s)==-EIO && s.owned);
      assert(s.cleanup_fault.address==0x05830238 && s.cleanup_fault.expected==0x02800500 && s.cleanup_fault.observed==0x07800780);
      puts("PASS: archived auto-mode capture exact first/cleanup BUF_LEN signatures reproduced; NO HW CLAIM");return 0;
    }
  assert(!a733_csi_capture_prepare(&s,&io,0x41000000,4*1024*1024));
  assert(s.armed && s.ready && !s.completed && !a733_csi_capture_check(&s));
  assert((values[slot(0x05821000)]&0x80008003)==0x80008003);
  assert((values[slot(0x05830204)]&0xf0c00)==0x10800);
  assert(values[slot(0x05830220)]==((0x41000000+64)>>2));
  emit=true;assert(!a733_csi_capture_wait(&s));emit=false;
  assert(!a733_csi_capture_finish(&s) && s.completed && !s.owned);
  assert(values[slot(0x05800004)]==0x100 && values[slot(0x05800800)]==1);
  unsigned all=calls;++cases;
  assert(s.sizing_active==1 && s.length_active==0x07800780);
  assert(!a733_csi_capture_write_mask(0x05830238) && !a733_csi_capture_write_mask(0x0583023c));
  assert(!s.sizing_restored && s.length_restored==0x02800500 && s.flip_restored==0x02d00500);
  for(unsigned mode=0;mode<8;++mode)
    {
      fresh();memset(&s,0,sizeof(s));values[slot(0x05830000)]=mode<<28;
      values[slot(0x05830238)]=0x01200240;values[slot(0x0583023c)]=0x01000180;
      uint32_t before_len=0,before_flip=0;
      assert(!rd(NULL,0x05830238,&before_len,10) && !rd(NULL,0x0583023c,&before_flip,10));
      assert(!a733_csi_capture_prepare(&s,&io,0x41000000,4*1024*1024));
      assert(!a733_csi_capture_check(&s));assert(!a733_csi_capture_finish(&s));
      assert(s.sizing_restored==(mode<<28) && s.length_restored==before_len && s.flip_restored==before_flip);++cases;
      assert(values[slot(0x05830238)]==0x01200240 && values[slot(0x0583023c)]==0x01000180);
    }
  for(unsigned at=1;at<=all;++at)
    {
      fresh();memset(&s,0,sizeof(s));fail_at=at;
      int r=a733_csi_capture_prepare(&s,&io,0x41000000,4*1024*1024);
      if(!r)r=a733_csi_capture_check(&s);
      if(!r){emit=true;r=a733_csi_capture_wait(&s);emit=false;}
      int cleanup=s.io?a733_csi_capture_finish(&s):0;
      assert(failed && (r<0 || cleanup<0));
      if(cleanup){assert(s.owned);unsigned n=calls;assert(a733_csi_capture_finish(&s)==cleanup && calls==n);}
      else assert(!s.owned);
      ++cases;
    }
  fresh();memset(&s,0,sizeof(s));translation=true;
  assert(a733_csi_capture_prepare(&s,&io,0x41000000,4*1024*1024)==-ENOTSUP);
  assert(!a733_csi_capture_finish(&s) && s.writes==2);++cases;
  fresh();memset(&s,0,sizeof(s));stale=true;
  assert(a733_csi_capture_prepare(&s,&io,0x41000000,4*1024*1024)==-ESTALE && !s.armed);
  stale=false;assert(!a733_csi_capture_finish(&s));++cases;
  fresh();memset(&s,0,sizeof(s));assert(!a733_csi_capture_prepare(&s,&io,0x41000000,4*1024*1024));
  assert(a733_csi_capture_wait(&s)==-ETIMEDOUT && !s.completed);
  assert(!a733_csi_capture_finish(&s));++cases;
  fresh();memset(&s,0,sizeof(s));assert(!a733_csi_capture_prepare(&s,&io,0x41000000,4*1024*1024));
  emit=error_packet=true;assert(a733_csi_capture_wait(&s)==-EBADMSG);emit=false;
  assert(!a733_csi_capture_finish(&s));++cases;
  fresh();memset(&s,0,sizeof(s));assert(!a733_csi_capture_prepare(&s,&io,0x41000000,4*1024*1024));
  drain_stuck=true;assert(a733_csi_capture_finish(&s)==-ETIMEDOUT && s.owned);++cases;
  fresh();memset(&s,0,sizeof(s));assert(!a733_csi_capture_prepare(&s,&io,0x41000000,4*1024*1024));
  values[slot(0x05830220)]^=16;
  assert(a733_csi_capture_check(&s)==-EACCES);assert(!a733_csi_capture_finish(&s));++cases;
  fresh();memset(&s,0,sizeof(s));assert(!a733_csi_capture_prepare(&s,&io,0x41000000,4*1024*1024));
  emit=true;assert(!a733_csi_capture_wait(&s));emit=false;late_error=true;
  assert(!a733_csi_capture_finish(&s) && !s.owned && !s.cleanup_error && s.error==-EBADMSG);
  assert(s.first_fault.address==0x05830254 && s.final_status==0x40);++cases;
  fresh();memset(&s,0,sizeof(s));assert(!a733_csi_capture_prepare(&s,&io,0x41000000,4*1024*1024));
  translation=true;assert(a733_csi_capture_check(&s)==-EACCES);
  assert(s.first_fault.address==0x03900014 && s.first_fault.expected==0 && s.first_fault.observed==1);
  assert(!a733_csi_capture_finish(&s) && !s.owned);++cases;
  assert(!a733_csi_capture_write_mask(0x03900014) && !a733_csi_capture_write_mask(0x03900604));
  fresh();memset(&s,0,sizeof(s));assert(!a733_csi_capture_prepare(&s,&io,0x41000000,4*1024*1024));
  emit=true;delayed_pause=35;assert(!a733_csi_capture_wait(&s));
  assert(s.completed && s.max_pause_ms==36 && s.schedule_delays==1);
  assert(!a733_csi_capture_finish(&s));++cases;
  fresh();memset(&s,0,sizeof(s));assert(!a733_csi_capture_prepare(&s,&io,0x41000000,4*1024*1024));
  emit=true;delayed_access=35;assert(!a733_csi_capture_wait(&s));
  assert(s.completed && s.max_access_ms==35);assert(!a733_csi_capture_finish(&s));++cases;
  fresh();memset(&s,0,sizeof(s));assert(!a733_csi_capture_prepare(&s,&io,0x41000000,4*1024*1024));
  delayed_pause=2100;assert(a733_csi_capture_wait(&s)==-ETIMEDOUT && !s.completed);
  assert(!a733_csi_capture_finish(&s));++cases;
  fresh();memset(&s,0,sizeof(s));assert(!a733_csi_capture_prepare(&s,&io,0x41000000,4*1024*1024));
  freeze=true;assert(a733_csi_capture_wait(&s)==-ETIMEDOUT);freeze=false;
  assert(!a733_csi_capture_finish(&s));++cases;
  printf("PASS: %u single-frame transaction cases including %u callback-after-effect faults; 35ms preemption, real deadline, frozen clock and safety gates; SYNTHETIC ONLY\n",cases,all);
}
