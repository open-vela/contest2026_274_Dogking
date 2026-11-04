/* SPDX-License-Identifier: Apache-2.0 */
/* Synthetic MMIO/PMIC model only; NEVER a physical camera test. */
#include "a733_imx214_cold.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static const uint32_t addresses[]={0x02002518,0x02000280,0x02000284,
  0x02000290,0x02003804,0x02002e8c,0x0251300c,0x02513010,
  0x02513014,0x02513018};
struct fake
{
  uint32_t values[10],now,frozen_at;
  uint8_t power[6];
  unsigned pr,reads,writes,fail_power,fail_read,fail_write;
  unsigned power_delay,mmio_delay,alter_power;
  int power_error;
  bool stuck_reset,freeze_on_reset,frozen,bad_clk,cleanup_fail;
};

static struct fake fresh(void)
{
  struct fake f={.values={0,0xffffffff,0xffffffff,0,0,0,0,0xf8,0,0},
    .power={3,0,0xff,0x17,0xc1,0x0d},.power_delay=1,.mmio_delay=1};
  return f;
}
static uint32_t now(void *p)
{struct fake *f=p;return f->frozen ? f->frozen_at : f->now;}
static int delay_ms(void *p,unsigned ms)
{assert(ms==1 || ms==10);((struct fake*)p)->now+=ms;return 0;}
static int index_of(uint32_t a)
{
  for (unsigned i=0;i<10;++i) if(addresses[i]==a) return (int)i;
  assert(!"Unexpected access, including CSI/ISP/DMA/TWI DATA");return -1;
}
static int pmic(void *p,uint8_t reg,uint8_t *out,unsigned ms)
{
  static const uint8_t regs[]={0x0e,0x0f,0x10,0x12,0x20,0x2a};
  struct fake *f=p;assert(ms==10);unsigned i=f->pr++ % 6;
  assert(reg==regs[i]);f->now+=f->power_delay;
  if(f->pr==f->fail_power)return f->power_error;
  *out=f->power[i];if(f->pr==f->alter_power)*out^=1;
  return 0;
}
static int rd(void *p,uint32_t a,uint32_t *out,unsigned ms)
{
  struct fake *f=p;assert(ms==10);int i=index_of(a);
  if(i>=6)assert((f->values[5]&0x10001)==0x10001); /* No gated-off TWI reads. */
  ++f->reads;f->now+=f->mmio_delay;
  if(f->reads==f->fail_read)return -EIO;
  *out=f->values[i];
  if(i==9 && !f->stuck_reset)*out=f->values[i]=0;
  if(i==8 && f->bad_clk)*out=0;
  return 0;
}
static int wr(void *p,uint32_t a,uint32_t v,unsigned ms)
{
  struct fake *f=p;assert(ms==10);int i=index_of(a);
  assert(i!=0 && i!=7); /* No APB parent or read-only status write. */
  if(i>=6)assert((f->values[5]&0x10001)==0x10001);
  if(i==6)assert(v==0 || v==0x48); /* Never START/STOP, DATA or interrupt enable. */
  if(i==8)assert(v==0x59);
  if(i==9)assert(v==1);
  ++f->writes;f->now+=f->mmio_delay;f->values[i]=v;
  if(i==9 && f->freeze_on_reset)
    {f->frozen=true;f->frozen_at=f->now;f->stuck_reset=true;}
  if(f->writes==f->fail_write || (f->cleanup_fail && i==1))return -EIO;
  return 0;
}
static struct a733_imx214_cold_io io(struct fake *f)
{return (struct a733_imx214_cold_io){.read_pmic8=pmic,.read32=rd,.write32=wr,
  .delay_ms=delay_ms,.now_ms=now,.context=f};}
static void restored(struct fake *f)
{
  assert(f->values[1]==0xffffffff && f->values[2]==0xffffffff);
  assert(!(f->values[3]&0x300) && !f->values[4] && !f->values[5]);
}

int main(void)
{
  unsigned cases=0,successful_writes=0,successful_reads=0;
  for(unsigned wrap=0;wrap<2;++wrap)
    {
      struct fake f=fresh();if(wrap)f.now=UINT32_MAX-5;
      struct a733_imx214_cold s={0};struct a733_imx214_cold_io b=io(&f);
      assert(a733_imx214_cold_prepare(&s,&b)==0);
      assert(s.state==A733_IMX214_COLD_PREPARED && s.controls_owned);
      assert(s.configured_mclk_hz==24000000 && f.pr==12);
      assert(strstr(s.stage,"NOT-measured"));
      assert((f.values[1]&0x00fff000)==0x00222000);
      assert((f.values[2]&0xff)==0x11 && !(f.values[3]&0x300));
      assert(f.values[4]==0x80000000 && f.values[5]==0x10001);
      successful_writes=f.writes;successful_reads=f.reads;
      unsigned before=f.pr;
      assert(a733_imx214_cold_prepare(&s,&b)==-EBUSY && f.pr==before);
      assert(a733_imx214_cold_clock(&s,&b,true)==0 && s.configured_mclk_hz==1000000);
      assert(f.values[4]==0x80000017 && !(f.values[3]&0x300));
      assert(a733_imx214_cold_clock(&s,&b,false)==0 && s.configured_mclk_hz==24000000);
      /* Other pin values changed between calls are preserved by masked restore. */
      f.values[3]|=0x80000000;
      assert(a733_imx214_cold_park(&s,&b)==0);
      restored(&f);assert(f.values[3]&0x80000000);
      assert(s.state==A733_IMX214_COLD_PARKED && !s.controls_owned);
      assert(a733_imx214_cold_prepare(&s,&b)==0);
      assert(a733_imx214_cold_park(&s,&b)==0);++cases;
    }
  for(unsigned at=1;at<=12;++at)
    for(unsigned positive=0;positive<2;++positive)
      {
        struct fake f=fresh();f.fail_power=at;f.power_error=positive?7:-ENOTSUP;
        struct a733_imx214_cold s={0};struct a733_imx214_cold_io b=io(&f);
        assert(a733_imx214_cold_prepare(&s,&b)==(positive?-EPROTO:-ENOTSUP));
        if(at<=6)assert(!f.reads && !f.writes);
        else restored(&f);
        assert(s.state==A733_IMX214_COLD_FAULT && !s.controls_owned);
        unsigned r=f.reads,w=f.writes;
        assert(a733_imx214_cold_prepare(&s,&b)==-EIO && r==f.reads && w==f.writes);
        ++cases;
      }
  for(unsigned i=0;i<6;++i)
    {
      struct fake f=fresh();f.power[i]^=(i==4 ? 0x40 : 1);
      struct a733_imx214_cold s={0};struct a733_imx214_cold_io b=io(&f);
      assert(a733_imx214_cold_prepare(&s,&b)<0 && !f.reads && !f.writes);++cases;
    }
  for(unsigned i=0;i<5;++i)
    {
      struct fake f=fresh();
      if(i==0)f.values[0]=0x01000000;
      if(i==1)f.values[1]&=~0xf000u;
      if(i==2)f.values[2]&=~0xf0u;
      if(i==3)f.values[4]=0x80000000;
      if(i==4)f.values[5]=0x10001;
      struct a733_imx214_cold s={0};struct a733_imx214_cold_io b=io(&f);
      assert(a733_imx214_cold_prepare(&s,&b)==-EBUSY && !f.writes);++cases;
    }
  for(unsigned at=1;at<=successful_writes;++at)
    {
      struct fake f=fresh();f.fail_write=at;
      struct a733_imx214_cold s={0};struct a733_imx214_cold_io b=io(&f);
      assert(a733_imx214_cold_prepare(&s,&b)==-EIO);
      restored(&f);assert(!s.controls_owned && s.state==A733_IMX214_COLD_FAULT);++cases;
    }
  for(unsigned at=1;at<=successful_reads;++at)
    {
      struct fake f=fresh();f.fail_read=at;
      struct a733_imx214_cold s={0};struct a733_imx214_cold_io b=io(&f);
      assert(a733_imx214_cold_prepare(&s,&b)==-EIO);
      restored(&f);assert(!s.controls_owned);++cases;
    }
  for(unsigned variant=0;variant<7;++variant)
    {
      struct fake f=fresh();
      if(variant==0)f.power_delay=11;
      if(variant==1)f.mmio_delay=11;
      if(variant==2)f.stuck_reset=true;
      if(variant==3)f.freeze_on_reset=true;
      if(variant==4)f.bad_clk=true;
      if(variant==5)f.alter_power=7;
      if(variant==6)f.values[7]=0x08;
      struct a733_imx214_cold s={0};struct a733_imx214_cold_io b=io(&f);
      assert(a733_imx214_cold_prepare(&s,&b)<0);
      if(variant!=1) {restored(&f);assert(!s.controls_owned);}
      ++cases;
    }
  {
    struct fake f=fresh();struct a733_imx214_cold s={0};
    struct a733_imx214_cold_io b=io(&f);
    assert(a733_imx214_cold_prepare(&s,&b)==0);
    f.cleanup_fail=true;
    assert(a733_imx214_cold_park(&s,&b)==-EIO);
    assert(s.controls_owned && s.state==A733_IMX214_COLD_FAULT);
    unsigned r=f.reads,w=f.writes;
    assert(a733_imx214_cold_park(&s,&b)==-EIO && r==f.reads && w==f.writes);++cases;
  }
  {
    struct fake f=fresh();struct a733_imx214_cold s={0};
    struct a733_imx214_cold_io b=io(&f);b.read_pmic8=NULL;b.firmware_power=f.power;
    assert(a733_imx214_cold_prepare(&s,&b)==0 && s.firmware_power_assumed && !f.pr);
    assert(!memcmp(s.power_before,f.power,6));
    for(unsigned i=0;i<6;++i)assert(!s.power_after[i]); /* No fake live resampling. */
    assert(a733_imx214_cold_park(&s,&b)==0);restored(&f);++cases;
  }
  for(unsigned write=1;write<=3;++write)
    {
      struct fake f=fresh();struct a733_imx214_cold s={0};
      struct a733_imx214_cold_io b=io(&f);
      assert(a733_imx214_cold_prepare(&s,&b)==0);
      f.fail_write=f.writes+write;
      assert(a733_imx214_cold_clock(&s,&b,true)==-EIO);
      restored(&f);assert(!s.controls_owned && !s.configured_mclk_hz);
      assert(s.state==A733_IMX214_COLD_FAULT);++cases;
    }
  {
    struct fake f=fresh();struct a733_imx214_cold s={0};
    struct a733_imx214_cold_io b=io(&f);
    assert(a733_imx214_cold_prepare(&s,&b)==0);
    f.fail_write=f.writes+1; /* Cleanup fails before it can stop MCLK. */
    assert(a733_imx214_cold_park(&s,&b)==-EIO);
    assert(s.controls_owned && s.configured_mclk_hz==24000000);
    assert(f.values[4]&0x80000000);++cases; /* Do not falsely claim gated. */
  }
  {
    struct fake f=fresh();struct a733_imx214_cold s={0};
    struct a733_imx214_cold_io b=io(&f);b.firmware_power=f.power;
    assert(a733_imx214_cold_prepare(&s,&b)==-EINVAL && !f.reads && !f.writes);++cases;
  }
  {
    struct fake f=fresh();struct a733_imx214_cold s={0};
    struct a733_imx214_cold_io b=io(&f);b.read_pmic8=NULL;
    assert(a733_imx214_cold_prepare(&s,&b)==-EINVAL && !f.reads && !f.writes);++cases;
  }
  printf("PASS: %u synthetic cold-control cases; power gating, owned RMW/readback, reset held, bounded waits/wrap, rollback/quarantine; NO real hardware/ID/CSI/DMA\n",cases);
  return 0;
}
