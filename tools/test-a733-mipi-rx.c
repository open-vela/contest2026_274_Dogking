/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "a733_mipi_rx.h"
#include "a733_imx214_cold.h"
static uint32_t addresses[80],values[80],ticks;
static unsigned used,calls,fail_at;static bool failed,emit,badcrc,stale,freeze,badpll;
static bool legacy_model;
static bool drop_delay;
static uint32_t moving(uint32_t a)
{
  if(a==0x05810004)return 0x001f0008;
  if(a==0x05810208)return 0x30000000;
  if(a==0x05810234)return 0xffff0000;
  return 0;
}
static unsigned index_of(uint32_t a)
{
  assert(a733_mipi_rx_readable(a));
  for(unsigned i=0;i<used;++i)if(addresses[i]==a)return i;
  assert(used<80);addresses[used]=a;return used++;
}
static int fault(void){++calls;if(calls==fail_at){failed=true;return -EIO;}return 0;}
static int read32(void *p,uint32_t a,uint32_t *v,unsigned ms)
{
  (void)p;(void)ms;unsigned i=index_of(a);
  if(a==0x05810234)values[i]=(values[i]&1)|((calls&65535)<<16);
  if(a==0x05810208)values[i]|=0x10000000;
  if(a==0x02002000)*v=badpll?0:0xf8675f00;
  else if(a==0x020020a0)*v=0xff126310;
  else if(a==0x05811518)*v=values[i]|(stale?4:0)|(emit?(badcrc?0x80f:0xf):0);
  else if(a==0x0581151c)*v=emit?0x2b:0;
  else *v=values[i];
  return fault();
}
static int write32(void *p,uint32_t a,uint32_t v,unsigned ms)
{
  (void)p;(void)ms;unsigned i=index_of(a);uint32_t mask=a733_mipi_rx_write_mask(a);
  assert(mask && ((v^values[i])&~(mask|moving(a)))==0);
  if(a==0x05811518)values[i]&=~v;
  else if(a==0x05810214)
    {
      /* DESKEW0: upper bits are not the DESKEW1 delay field. */
      values[i]=(values[i]&~0xfff)|(v&0xfff);
      if(legacy_model)values[index_of(0x05810004)]^=0x10000;
    }
  else if(a==0x05810218 && drop_delay) { /* ignores a config write, not RO */ }
  else
    {
      values[i]=(values[i]&moving(a))|(v&~moving(a));
      if(a==0x05810004 && (v&2))values[i]=(v&0x1f7)|((v&0x1f0)<<12)|8;
    }
  return fault(); /* failure AFTER a possibly completed write */
}
static uint32_t now(void *p){(void)p;return ticks;}
static int delay(void *p,unsigned ms){(void)p;if(!freeze)ticks+=ms;return fault();}
static const struct a733_imx214_cold_io io={.read32=read32,.write32=write32,.now_ms=now,.delay_ms=delay};
static void fresh(void)
{memset(addresses,0,sizeof(addresses));memset(values,0,sizeof(values));used=calls=fail_at=ticks=0;failed=emit=badcrc=stale=freeze=badpll=legacy_model=drop_delay=false;}
int main(int argc,char **argv)
{
  unsigned cases=0;
  if(argc==2)
    {
      assert(!strcmp(argv[1],"legacy"));fresh();legacy_model=true;
      struct a733_mipi_rx old={0};assert(a733_mipi_rx_prepare(&old,&io)==-EIO);
      assert(a733_mipi_rx_finish(&old)==-EIO && old.owned && !old.ready);
      assert(old.reads==93 && old.writes==29 && old.last_address==0x05810004);
      puts("PASS: archived v195 code reproduces 93 reads/29 writes and cleanup address 05810004 in reserved-bit/live-result MODEL; NOT new board evidence");
      return 0;
    }
  fresh();struct a733_mipi_rx s={0};assert(!a733_mipi_rx_prepare(&s,&io));
  unsigned preparation=calls;emit=true;assert(!a733_mipi_rx_observe(&s));
  assert(s.raw_headers==500 && s.fs==500 && s.fe==500 && !s.errors);
  emit=false;assert(!a733_mipi_rx_finish(&s) && !s.owned);unsigned all=calls;++cases;
  for(unsigned at=1;at<=all;++at)
    {
      fresh();memset(&s,0,sizeof(s));fail_at=at;
      int r=a733_mipi_rx_prepare(&s,&io);
      if(!r){emit=true;r=a733_mipi_rx_observe(&s);emit=false;}
      int cleanup=a733_mipi_rx_finish(&s);
      assert(failed && (r<0 || cleanup<0));
      if(cleanup){assert(s.owned);unsigned n=calls;assert(a733_mipi_rx_finish(&s)==cleanup && calls==n);}
      else assert(!s.owned);
      ++cases;
    }
  fresh();memset(&s,0,sizeof(s));stale=true;assert(a733_mipi_rx_prepare(&s,&io)==-ESTALE);
  stale=false;assert(!a733_mipi_rx_finish(&s));++cases;
  fresh();memset(&s,0,sizeof(s));assert(!a733_mipi_rx_prepare(&s,&io));
  assert(a733_mipi_rx_observe(&s)==-ENODATA);assert(!a733_mipi_rx_finish(&s));++cases;
  fresh();memset(&s,0,sizeof(s));assert(!a733_mipi_rx_prepare(&s,&io));emit=badcrc=true;
  assert(a733_mipi_rx_observe(&s)==-EBADMSG);emit=false;assert(!a733_mipi_rx_finish(&s));++cases;
  fresh();memset(&s,0,sizeof(s));freeze=true;assert(a733_mipi_rx_prepare(&s,&io)==-ETIMEDOUT);
  assert(!a733_mipi_rx_finish(&s));++cases;
  fresh();memset(&s,0,sizeof(s));badpll=true;
  assert(a733_mipi_rx_prepare(&s,&io)==-ERANGE && !s.writes && !s.owned);++cases;
  fresh();memset(&s,0,sizeof(s));values[index_of(0x02003844)]=0x10001;
  assert(a733_mipi_rx_prepare(&s,&io)==-EBUSY && !s.writes && !s.owned);++cases;
  fresh();memset(&s,0,sizeof(s));assert(!a733_mipi_rx_prepare(&s,&io));
  values[index_of(0x05811404)]^=1;
  assert(a733_mipi_rx_check(&s)==-EACCES);assert(!a733_mipi_rx_finish(&s));++cases;
  fresh();memset(&s,0,sizeof(s));values[index_of(0x02000584)]=0x55555555;
  assert(a733_mipi_rx_prepare(&s,&io)==-EBUSY);assert(!a733_mipi_rx_finish(&s));++cases;
  fresh();memset(&s,0,sizeof(s));values[index_of(0x02000584)]=0xffffffab;
  values[index_of(0x02000588)]=0x9876ffff;
  assert(!a733_mipi_rx_prepare(&s,&io));assert(!a733_mipi_rx_finish(&s));
  assert(values[index_of(0x02000584)]==0xffffffab && values[index_of(0x02000588)]==0x9876ffff);++cases;
  assert(!a733_mipi_rx_write_mask(0x05830000) && !a733_mipi_rx_readable(0x03900000));
  assert(!a733_mipi_rx_write_mask(0x05810214));
  assert(a733_mipi_rx_write_mask(0x05810218)==0x01f00000);
  assert(a733_mipi_rx_write_mask(0x05810004)==0x1f7);
  fresh();memset(&s,0,sizeof(s));drop_delay=true;
  assert(a733_mipi_rx_prepare(&s,&io)==-EIO);
  assert(s.first_fault.error==-EIO && s.first_fault.address==0x05810218 &&
    s.first_fault.mask==0x01f00000 && s.first_fault.expected==0x00700000);
  drop_delay=false;assert(!a733_mipi_rx_finish(&s) && !s.cleanup_fault.error);
  assert(s.first_fault.address==0x05810218);++cases;
  fresh();memset(&s,0,sizeof(s));assert(!a733_mipi_rx_prepare(&s,&io));
  drop_delay=true;assert(a733_mipi_rx_finish(&s)==-EIO);
  assert(!s.first_fault.error && s.cleanup_fault.address==0x05810218 && s.owned);++cases;
  printf("PASS: %u receiver cases, %u prepare callbacks, %u callback-after-effect failures; bounded/no-data/stale/CRC/cleanup; SYNTHETIC ONLY\n",cases,preparation,all);
  return 0;
}
