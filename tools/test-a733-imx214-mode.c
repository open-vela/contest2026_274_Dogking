/* SPDX-License-Identifier: Apache-2.0 */
/* Synthetic mode/receiver, NOT physical sensor or DMA acceptance. */
#include "a733_imx214_mode.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint16_t regs[]={0x0101,0x0112,0x0113,0x0114,0x0136,0x0137,
  0x0340,0x0341,0x0342,0x0343,0x034c,0x034d,0x034e,0x034f};
static const uint8_t vals[]={3,10,10,3,24,0,8,0x50,0x13,0x90,7,0x80,4,0x38};
static uint8_t memory[65536];
static uint32_t crc(const uint8_t *p,size_t n)
{
  uint32_t c=~0u;
  while(n--){c^=*p++;for(unsigned i=0;i<8;++i)c=(c>>1)^(0xedb88320u&(0u-(c&1)));}
  return ~c;
}
static void put16(uint8_t *p,unsigned v){p[0]=v;p[1]=v>>8;}
static void put32(uint8_t *p,uint32_t v){put16(p,v);put16(p+2,v>>16);}
static void fix(uint8_t *p,size_t size)
{put32(p+16,crc(p+48,size-48));put32(p+44,crc(p,44));}
static size_t fixture(uint8_t *p)
{
  size_t n=48+sizeof(vals)*4;
  memset(p,0,n);memcpy(p,"A7CM",4);put16(p+4,1);put16(p+6,48);
  put32(p+8,n);put32(p+12,sizeof(vals));put16(p+20,0x214);
  put16(p+22,1920);put16(p+24,1080);put16(p+26,5008);put16(p+28,2128);
  p[30]=24;p[31]=4;p[32]=10;p[33]=3;put32(p+36,800);
  for(unsigned i=0;i<sizeof(vals);++i)
    {p[48+i*4]=1;p[49+i*4]=vals[i];put16(p+50+i*4,regs[i]);}
  fix(p,n);return n;
}

struct fake
{
  uint32_t now;
  unsigned reads,writes,fail_read,fail_write,ready,stopped;
  unsigned io_delay;
  int ready_ret,stop_ret;
  bool wrong_id,bad_readback,wrong_stream,slow_ready,delay_does_not_wait;
  uint8_t *mutate;
};
static uint32_t now(void *arg){return ((struct fake*)arg)->now;}
static int rd(void *arg,uint16_t reg,uint8_t *data,size_t n,unsigned budget)
{
  struct fake *f=arg;++f->reads;f->now+=f->io_delay?f->io_delay:1;assert(budget==30);
  if(f->mutate){put16(f->mutate+50,0x0100);f->mutate=NULL;}
  if(f->fail_read==f->reads)return -EIO;
  if(reg==0x0016){assert(n==2);data[0]=2;data[1]=f->wrong_id?0x15:0x14;}
  else
    {assert(n==1);data[0]=memory[reg];
     if(f->bad_readback && reg==0x034e)data[0]^=1;
     if(f->wrong_stream && reg==0x0100 && memory[reg]==1)data[0]=0;}
  return 0;
}
static int wr(void *arg,uint16_t reg,uint8_t value,unsigned budget)
{
  struct fake *f=arg;++f->writes;f->now+=f->io_delay?f->io_delay:1;assert(budget==30);
  if(f->fail_write==f->writes)return -EIO;
  memory[reg]=value;return 0;
}
static int ready(void *arg,unsigned budget)
{
  struct fake *f=arg;assert(budget==100 && !memory[0x0100]);++f->ready;
  f->now+=f->slow_ready?101:1;return f->ready_ret;
}
static int stop(void *arg,unsigned budget)
{
  struct fake *f=arg;assert(budget==100 && !memory[0x0100]);++f->stopped;
  ++f->now;return f->stop_ret;
}
static int delay_ms(void *arg,unsigned ms)
{
  struct fake *f=arg;
  if(!f->delay_does_not_wait)f->now+=ms;
  return 0;
}
static struct a733_imx214_mode_io io(struct fake *f)
{
  memset(memory,0,sizeof(memory));
  return (struct a733_imx214_mode_io){.read=rd,.write8=wr,.now_ms=now,.delay_ms=delay_ms,
    .receiver_ready=ready,.receiver_stop=stop,.context=f,
    .power_clock_qualified=true,.exclusive_sensor_and_receiver=true,
    .artifact_sha256_verified=true};
}

int main(int argc,char **argv)
{
  uint8_t data[2096];size_t n=fixture(data);
  struct a733_imx214_mode m;
  assert(!a733_imx214_mode_parse(data,n,&m) && m.count==14);
  for(unsigned gate=0;gate<3;++gate)
    {
      struct fake f={0};struct a733_imx214_mode_io ops=io(&f);
      struct a733_imx214_session s={0};
      if(gate==0)ops.power_clock_qualified=false;
      if(gate==1)ops.exclusive_sensor_and_receiver=false;
      if(gate==2)ops.artifact_sha256_verified=false;
      assert(a733_imx214_mode_prepare(&s,&ops,data,n)==-EACCES);
      assert(!f.reads && !f.writes);
    }
  for(unsigned wrap=0;wrap<2;++wrap)
    {
      struct fake f={.now=wrap?UINT32_MAX-10:0};
      struct a733_imx214_mode_io ops=io(&f);struct a733_imx214_session s={0};
      assert(!a733_imx214_mode_prepare(&s,&ops,data,n));
      assert(s.state==A733_IMX214_PREPARED && !memory[0x0100] && f.writes==14);
      for(unsigned round=0;round<2;++round)
        {
          assert(!a733_imx214_mode_start(&s,&ops));
          assert(memory[0x0100]==1 && s.retain_resources);
          assert(!a733_imx214_mode_stop(&s,&ops));
          assert(!memory[0x0100] && !s.retain_resources && s.state==A733_IMX214_PREPARED);
        }
    }
  for(unsigned kind=0;kind<4;++kind)
    {
      n=fixture(data);
      if(kind==0)data[49]^=1; /* payload CRC */
      if(kind==1){put16(data+50,0x0100);fix(data,n);} /* hidden stream-on */
      if(kind==2){put16(data+50,0x0103);fix(data,n);} /* reset */
      if(kind==3){data[49]=0;fix(data,n);} /* last effective orientation */
      struct fake f={0};struct a733_imx214_mode_io ops=io(&f);
      struct a733_imx214_session s={0};
      assert(a733_imx214_mode_prepare(&s,&ops,data,n)<0 && !f.reads && !f.writes);
    }
  n=fixture(data);
  struct fake mutate={.mutate=data};struct a733_imx214_mode_io ops=io(&mutate);
  struct a733_imx214_session s={0};
  assert(!a733_imx214_mode_prepare(&s,&ops,data,n) && !memory[0x0100]);
  n=fixture(data);
  for(unsigned stage=1;stage<=14;++stage)
    {
      struct fake f={.fail_write=stage};ops=io(&f);s=(struct a733_imx214_session){0};
      assert(a733_imx214_mode_prepare(&s,&ops,data,n)==-EIO);
      assert(s.state==A733_IMX214_FAULT && s.retain_resources && !memory[0x0100]);
      unsigned w=f.writes,r=f.reads;
      assert(a733_imx214_mode_start(&s,&ops)==-EBUSY && f.writes==w && f.reads==r);
    }
  for(unsigned stage=1;stage<=17;++stage)
    {
      struct fake f={.fail_read=stage};ops=io(&f);s=(struct a733_imx214_session){0};
      assert(a733_imx214_mode_prepare(&s,&ops,data,n)==-EIO && s.retain_resources);
    }
  struct fake bad_id={.wrong_id=true};ops=io(&bad_id);s=(struct a733_imx214_session){0};
  assert(a733_imx214_mode_prepare(&s,&ops,data,n)==-ENODEV && !bad_id.writes);
  struct fake mismatch={.bad_readback=true};ops=io(&mismatch);s=(struct a733_imx214_session){0};
  assert(a733_imx214_mode_prepare(&s,&ops,data,n)==-EIO && s.retain_resources);
  struct fake busy={.ready_ret=-EBUSY};ops=io(&busy);s=(struct a733_imx214_session){0};
  assert(!a733_imx214_mode_prepare(&s,&ops,data,n));
  assert(a733_imx214_mode_start(&s,&ops)==-EBUSY && !memory[0x0100]);
  assert(s.state==A733_IMX214_PREPARED && !s.retain_resources);
  struct fake late={.slow_ready=true};ops=io(&late);s=(struct a733_imx214_session){0};
  assert(!a733_imx214_mode_prepare(&s,&ops,data,n));
  assert(a733_imx214_mode_start(&s,&ops)==-ETIMEDOUT && s.retain_resources);
  assert(!a733_imx214_mode_stop(&s,&ops) && !s.retain_resources && s.error==-ETIMEDOUT);
  struct fake stop_fail={.stop_ret=-EIO};ops=io(&stop_fail);s=(struct a733_imx214_session){0};
  assert(!a733_imx214_mode_prepare(&s,&ops,data,n) && !a733_imx214_mode_start(&s,&ops));
  assert(a733_imx214_mode_stop(&s,&ops)==-EIO && s.retain_resources);
  unsigned calls=stop_fail.writes+stop_fail.reads;
  assert(a733_imx214_mode_stop(&s,&ops)==-EBUSY && calls==stop_fail.writes+stop_fail.reads);
  struct fake stream={.wrong_stream=true};ops=io(&stream);s=(struct a733_imx214_session){0};
  assert(!a733_imx214_mode_prepare(&s,&ops,data,n));
  assert(a733_imx214_mode_start(&s,&ops)==-EIO && s.retain_resources);
  assert(!a733_imx214_mode_stop(&s,&ops) && !s.retain_resources && s.error==-EIO);
  struct fake trust={0};ops=io(&trust);s=(struct a733_imx214_session){0};
  assert(!a733_imx214_mode_prepare(&s,&ops,data,n) && !a733_imx214_mode_start(&s,&ops));
  ops.artifact_sha256_verified=false;
  assert(!a733_imx214_mode_stop(&s,&ops) && !s.retain_resources && !memory[0x0100]);
  struct fake owner={0};ops=io(&owner);s=(struct a733_imx214_session){0};
  memory[0x0100]=1;
  assert(a733_imx214_mode_prepare(&s,&ops,data,n)==-EBUSY && !owner.writes);
  n=fixture(data);data[n]=2;data[n+1]=0;put16(data+n+2,50);
  n+=4;put32(data+8,n);put32(data+12,15);fix(data,n);
  struct fake delay={0};ops=io(&delay);s=(struct a733_imx214_session){0};
  assert(!a733_imx214_mode_prepare(&s,&ops,data,n));
  struct fake no_delay={.delay_does_not_wait=true};ops=io(&no_delay);
  s=(struct a733_imx214_session){0};
  assert(a733_imx214_mode_prepare(&s,&ops,data,n)==-EPROTO && s.retain_resources);
  n=fixture(data);
  for(unsigned i=14;i<512;++i)
    {data[48+i*4]=1;data[49+i*4]=0;put16(data+50+i*4,0x0401);}
  n=sizeof(data);put32(data+8,n);put32(data+12,512);fix(data,n);
  struct fake deadline={.io_delay=29};ops=io(&deadline);s=(struct a733_imx214_session){0};
  assert(a733_imx214_mode_prepare(&s,&ops,data,n)==-ETIMEDOUT);
  assert(deadline.now<=5000 && deadline.writes<512 && !memory[0x0100] && s.retain_resources);
  if(argc==2)
    {
      FILE *f=fopen(argv[1],"rb");assert(f);
      size_t size=fread(data,1,sizeof(data),f);assert(fgetc(f)==EOF && !ferror(f));assert(!fclose(f));
      assert(!a733_imx214_mode_parse(data,size,&m));
      printf("PRIVATE reference parsed only: ops=%u crc=%08x; NO execution\n",m.count,m.crc32);
    }
  puts("PASS: actual mode parser/prepare/readback/start/stop, corrupted/hidden writes, immutable copy, receiver gates, failure retention; SYNTHETIC only");
  return 0;
}
