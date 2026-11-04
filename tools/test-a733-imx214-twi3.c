/* SPDX-License-Identifier: Apache-2.0 */
/* Synthetic controller, NOT physical I2C/sensor/board qualification. */
#include "a733_imx214_twi3.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

struct fake
{
  uint32_t now, control, status, pending;
  unsigned reads, writes, phase, index, position, stops;
  bool wrong_id, nack, stall, frozen, stop_stall, bad_mux, arb, writing;
};

static const uint16_t regs[]={0x0016,0x0100,0x0112,0x0114,0x034c};
static const unsigned lengths[]={2,1,2,1,4};
static const uint8_t values[5][4]={{2,0x14},{0},{10,10},{3},{7,0x80,4,0x38}};

static int rd(void *arg,uint32_t address,uint32_t *value)
{
  struct fake *f=arg;++f->reads;
  switch(address)
    {
      case 0x02002e8c:*value=0x10001;return 0;
      case 0x02000280:*value=f->bad_mux?0:0x00222000;return 0;
      case 0x02000284:*value=0x11;return 0;
      case 0x02000290:*value=0x300;return 0;
      case 0x02003804:*value=0x80000000;return 0;
      case 0x02002518:*value=0;return 0;
      case 0x02513014:*value=0x59;return 0; /* M=11,N=1: 24M/(10*12*2)=100k */
      case 0x0251300c:*value=f->stall?0x40:f->control;return 0;
      case 0x02513010:*value=f->status;return 0;
      case 0x02513008:
        assert(f->index<5 && f->position<lengths[f->index]);
        *value=values[f->index][f->position++];
        if(f->wrong_id && f->index==0 && f->position==2) *value=0x15;
        return 0;
      default:assert(!"unlisted MMIO read");return -EFAULT;
    }
}

static int wr(void *arg,uint32_t address,uint32_t value)
{
  struct fake *f=arg;++f->writes;
  assert(address==0x02513008 || address==0x0251300c);
  if(address==0x02513008) {f->pending=value;return 0;}
  if(value==0x58)
    {
      ++f->stops;
      f->control=f->stop_stall?0x50:0x40;
      f->status=0xf8;f->phase=0;f->position=0;++f->index;
      return 0;
    }
  f->control=0x48;
  assert(f->index<5 || f->writing);
  switch(f->phase)
    {
      case 0: assert(value==0x68);f->status=0x08;break;
      case 1: assert(value==0x48 && f->pending==0x20);
        f->status=f->arb?0x38:f->nack?0x20:0x18;break;
      case 2: assert(value==0x48 && f->pending==(f->writing?3:(regs[f->index]>>8)));
        f->status=0x28;break;
      case 3: assert(value==0x48 && f->pending==(f->writing?0x4d:(regs[f->index]&255)));
        f->status=0x28;break;
      case 4:
        if(f->writing){assert(value==0x48 && f->pending==0x80);f->status=0x28;}
        else {assert(value==0x68);f->status=0x10;}
        break;
      case 5: assert(value==0x48 && f->pending==0x21);f->status=0x40;break;
      default:
        assert(f->position<lengths[f->index]);
        if(f->position+1<lengths[f->index])
          {assert(value==0x4c);f->status=0x50;}
        else {assert(value==0x48);f->status=0x58;}
    }
  ++f->phase;
  return 0;
}

static uint32_t now(void *arg)
{
  struct fake *f=arg;
  if(!f->frozen) ++f->now;
  return f->now;
}

static struct a733_imx214_twi3 bus(struct fake *f)
{
  f->control=0x40;f->status=0xf8;
  return (struct a733_imx214_twi3){.read32=rd,.write32=wr,.now_ms=now,
    .context=f,.board_power_qualified=true,.exclusive_sensor_and_bus=true,
    .measured_mclk_hz=24000000};
}

int main(void)
{
  struct a733_imx214_probe p;
  for(unsigned wrap=0;wrap<2;++wrap)
    {
      struct fake f={.now=wrap?UINT32_MAX-20:0};
      struct a733_imx214_twi3 b=bus(&f);
      int result=a733_imx214_twi3_identify(&p,&b);
      if(result) fprintf(stderr,"result=%d stage=%s elapsed=%u reads=%u writes=%u\n",
                         result,p.stage,p.elapsed_ms,f.reads,f.writes);
      assert(result==0);
      assert(p.identity_match && p.snapshot_complete && p.chip_id==0x214);
      assert(p.width==1920 && p.height==1080 && p.wire_format==0x0a0a);
      assert(f.stops==5 && f.index==5 && !b.quarantined);
      assert(b.identity_verified);
    }
  for(unsigned gate=0;gate<4;++gate)
    {
      struct fake f={0};struct a733_imx214_twi3 b=bus(&f);
      if(gate==0)b.board_power_qualified=false;
      if(gate==1)b.exclusive_sensor_and_bus=false;
      if(gate==2)b.measured_mclk_hz=26000000;
      if(gate==3)b.quarantined=true;
      assert(a733_imx214_twi3_identify(&p,&b)==-EACCES);
      assert(f.reads==0 && f.writes==0);
    }
  struct fake mux={.bad_mux=true};struct a733_imx214_twi3 b=bus(&mux);
  assert(a733_imx214_twi3_identify(&p,&b)==-EACCES && mux.writes==0);
  struct fake id={.wrong_id=true};b=bus(&id);
  assert(a733_imx214_twi3_identify(&p,&b)==-ENODEV && id.stops==1);
  struct fake nack={.nack=true};b=bus(&nack);
  assert(a733_imx214_twi3_identify(&p,&b)==-ENXIO && nack.stops==1);
  assert(!b.quarantined);
  struct fake arb={.arb=true};b=bus(&arb);
  assert(a733_imx214_twi3_identify(&p,&b)==-EIO && arb.stops==0);
  assert(b.quarantined);
  for(unsigned frozen=0;frozen<2;++frozen)
    {
      struct fake stuck={.stall=true,.frozen=frozen};b=bus(&stuck);
      assert(a733_imx214_twi3_identify(&p,&b)==-ETIMEDOUT);
      assert(b.quarantined && stuck.reads<100100);
      unsigned writes=stuck.writes, reads=stuck.reads;
      assert(a733_imx214_twi3_identify(&p,&b)==-EACCES);
      assert(stuck.writes==writes && stuck.reads==reads);
    }
  struct fake stop={.stop_stall=true};b=bus(&stop);
  assert(a733_imx214_twi3_identify(&p,&b)==-ETIMEDOUT && b.quarantined);
  struct fake writer={0};b=bus(&writer);
  assert(a733_imx214_twi3_write8(&b,0x034d,0x80,100)==-EACCES);
  assert(!writer.reads && !writer.writes);
  assert(!a733_imx214_twi3_identify(&p,&b));
  unsigned old_writes=writer.writes;
  assert(a733_imx214_twi3_write8(&b,0x0103,1,100)==-EACCES);
  assert(a733_imx214_twi3_write8(&b,0x0016,1,100)==-EACCES);
  assert(a733_imx214_twi3_write8(&b,0x0100,2,100)==-EACCES);
  assert(writer.writes==old_writes);
  writer.writing=true;
  assert(!a733_imx214_twi3_write8(&b,0x034d,0x80,100));
  assert(writer.stops==6 && !b.quarantined && b.identity_verified);
  struct fake reject={0};b=bus(&reject);
  assert(!a733_imx214_twi3_identify(&p,&b));
  reject.writing=true;reject.nack=true;
  assert(a733_imx214_twi3_write8(&b,0x034d,0x80,100)==-ENXIO);
  assert(!b.identity_verified && reject.stops==6);
  old_writes=reject.writes;
  assert(a733_imx214_twi3_write8(&b,0x034d,0x80,100)==-EACCES);
  assert(reject.writes==old_writes);
  struct fake changed={0};b=bus(&changed);
  assert(!a733_imx214_twi3_identify(&p,&b));
  changed.bad_mux=true;old_writes=changed.writes;
  assert(a733_imx214_twi3_write8(&b,0x034d,0x80,100)==-EACCES);
  assert(!b.identity_verified && changed.writes==old_writes);
  changed.bad_mux=false;
  assert(a733_imx214_twi3_write8(&b,0x034d,0x80,100)==-EACCES);
  assert(changed.writes==old_writes);
  struct fake trial={0};b=bus(&trial);
  b.board_power_qualified=false;b.measured_mclk_hz=0;
  assert(!a733_imx214_twi3_trial_id(&p,&b));
  assert(p.identity_match && !p.snapshot_complete && trial.stops==1);
  assert(!b.identity_verified && !b.board_power_qualified && !b.measured_mclk_hz);
  old_writes=trial.writes;
  assert(a733_imx214_twi3_write8(&b,0x0100,1,100)==-EACCES);
  assert(a733_imx214_twi3_trial_id(&p,&b)==-EALREADY && trial.writes==old_writes);
  assert(a733_imx214_twi3_identify(&p,&b)==-EACCES && trial.writes==old_writes);
  puts("PASS: exploratory ID never creates a strict qualification/identity lease or mode/stream permission");
  puts("PASS: v187 identity-gated write8, BE16 register/value/STOP, forbidden reset/OTP, write failure invalidates identity; SYNTHETIC only");
  puts("PASS: actual TWI3 transaction core, BE16/repeated START/ACK/NACK/STOP, zero-write gates, timeout/quarantine/arbitration; SYNTHETIC only");
  return 0;
}
