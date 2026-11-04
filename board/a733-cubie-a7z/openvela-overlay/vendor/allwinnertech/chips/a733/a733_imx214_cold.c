/* SPDX-License-Identifier: Apache-2.0 */
#include "a733_imx214_cold.h"
#include "a733_imx214_twi3.h"
#include <errno.h>
#include <string.h>

#define APB1  0x02002518u
#define CFG0  0x02000280u
#define CFG1  0x02000284u
#define PDATA 0x02000290u
#define MCLK1 0x02003804u
#define BGR3  0x02002e8cu
#define TWI3  0x02513000u
#define CTL   (TWI3+0x0cu)
#define STAT  (TWI3+0x10u)
#define CLK   (TWI3+0x14u)
#define SRST  (TWI3+0x18u)
#define MCLK_MASK 0x87001f1fu
#define PIN_MASK  0x00fff000u
#define CONTROL_MASK 0x300u

static const uint8_t power_regs[6] = {0x0e,0x0f,0x10,0x12,0x20,0x2a};
static const uint32_t saved_regs[6] = {APB1,CFG0,CFG1,PDATA,MCLK1,BGR3};

static int result(int ret) {return ret > 0 ? -EPROTO : ret;}

static int time_left(struct a733_imx214_cold *s,
                      const struct a733_imx214_cold_io *io)
{
  s->elapsed_ms = io->now_ms(io->context)-s->started_ms;
  return s->elapsed_ms >= (s->budget_ms ? s->budget_ms : 250) ? -ETIMEDOUT : 0;
}

static int rd(struct a733_imx214_cold *s,
                const struct a733_imx214_cold_io *io,
                uint32_t address, uint32_t *value)
{
  if (!s->cleaning && time_left(s,io)) return -ETIMEDOUT;
  s->last_address=address;
  ++s->reads;
  uint32_t start=io->now_ms(io->context);
  int ret=result(io->read32(io->context,address,value,10));
  if (!ret && io->now_ms(io->context)-start>10) ret=-ETIMEDOUT;
  return ret;
}

static bool writable(uint32_t a)
{
  return a==CFG0 || a==CFG1 || a==PDATA || a==MCLK1 || a==BGR3 ||
         a==CTL || a==CLK || a==SRST;
}

static int wr(struct a733_imx214_cold *s,
                const struct a733_imx214_cold_io *io,
                uint32_t address,uint32_t value)
{
  if (!writable(address)) return -EACCES;
  if (!s->cleaning && time_left(s,io)) return -ETIMEDOUT;
  s->dirty=true; /* A failing write callback may have partly taken effect. */
  s->last_address=address;
  ++s->writes;
  uint32_t start=io->now_ms(io->context);
  int ret=result(io->write32(io->context,address,value,10));
  if (!ret && io->now_ms(io->context)-start>10) ret=-ETIMEDOUT;
  return ret;
}

/* Masked RMW + readback never alters another pin or a shared APB parent. */
static int update(struct a733_imx214_cold *s,
                    const struct a733_imx214_cold_io *io,
                    uint32_t a,uint32_t mask,uint32_t value)
{
  uint32_t before,after;
  int ret=rd(s,io,a,&before);
  if (!ret) ret=wr(s,io,a,(before & ~mask) | (value & mask));
  if (!ret) ret=rd(s,io,a,&after);
  if (!ret && (after & mask)!=(value & mask)) ret=-EIO;
  return ret;
}

static int power(struct a733_imx214_cold *s,
                   const struct a733_imx214_cold_io *io,uint8_t out[6])
{
  for (unsigned i=0;i<6;++i)
    {
      int ret=time_left(s,io);
      if (ret) return ret;
      if (io->firmware_power)
        {out[i]=io->firmware_power[i];continue;}
      uint32_t start=io->now_ms(io->context);
      ret=result(io->read_pmic8(io->context,power_regs[i],out+i,10));
      if (!ret && io->now_ms(io->context)-start>10) ret=-ETIMEDOUT;
      if (ret) return ret;
    }
  if (out[0]!=3 || out[1]!=0) return -ENODEV;
  if (!(out[2]&1) || !(out[4]&0x40)) return -EACCES;
  if ((out[3]&31)!=23 || (out[5]&31)!=13) return -ERANGE;
  return time_left(s,io);
}

static int delay(struct a733_imx214_cold *s,
                   const struct a733_imx214_cold_io *io,unsigned ms)
{
  uint32_t start=io->now_ms(io->context);
  int ret=result(io->delay_ms(io->context,ms));
  uint32_t elapsed=io->now_ms(io->context)-start;
  if (!ret && (elapsed<ms || elapsed>ms+10)) ret=-ETIMEDOUT;
  return ret ? ret : time_left(s,io);
}

static bool valid(const struct a733_imx214_cold_io *io)
{
  return io && (!!io->read_pmic8 != !!io->firmware_power) && io->read32 && io->write32 &&
         io->delay_ms && io->now_ms;
}

int a733_imx214_cold_park(struct a733_imx214_cold *s,
                         const struct a733_imx214_cold_io *io)
{
  if (!s || !valid(io)) return -EINVAL;
  if (s->cleanup_error) return s->cleanup_error; /* Reboot, not a cleanup retry. */
  if (s->bus_quarantined) return -EIO;
  if (!s->controls_owned)
    return s->state==A733_IMX214_COLD_FAULT ? s->error : 0;
  /* No DMA exists; a v193 ID experiment may have finished its single read.
   * Stop the known-idle TWI, assert reset
   * before stopping MCLK, restore only owned pin fields, then gate TWI.
   * Abort on failed cleanup: ownership remains held, never guess idle.
   */
  s->cleaning=true;
  int ret=s->twi_accessible ? wr(s,io,CTL,0) : 0;
  if (!ret) ret=update(s,io,PDATA,CONTROL_MASK,0);
  if (!ret) ret=update(s,io,MCLK1,0x80000000u,0);
  if (!ret) s->configured_mclk_hz=0;
  if (!ret) ret=update(s,io,MCLK1,MCLK_MASK,s->original[4]);
  if (!ret) ret=update(s,io,CFG0,PIN_MASK,s->original[1]);
  if (!ret) ret=update(s,io,CFG1,0xff,s->original[2]);
  if (!ret) ret=update(s,io,PDATA,CONTROL_MASK,s->original[3]);
  if (!ret) ret=update(s,io,BGR3,0x10001,s->original[5]);
  s->cleaning=false;
  s->cleanup_error=ret;
  if (ret)
    {
      if (!s->error) s->error=ret;
      s->state=A733_IMX214_COLD_FAULT;
      s->stage="cleanup-failed-ownership-retained";
    }
  else
    {
      s->controls_owned=false;
      s->twi_accessible=false;
      s->dirty=false;
      if (s->state!=A733_IMX214_COLD_FAULT)
        {s->state=A733_IMX214_COLD_PARKED;s->stage="controls-restored-no-sensor-transfer";}
    }
  return ret;
}

int a733_imx214_cold_prepare(struct a733_imx214_cold *s,
                            const struct a733_imx214_cold_io *io)
{
  if (!s || !valid(io)) return -EINVAL;
  if (s->state==A733_IMX214_COLD_FAULT) return -EIO;
  if (s->controls_owned || s->state==A733_IMX214_COLD_PREPARED) return -EBUSY;
  if (s->state!=A733_IMX214_COLD_NEW && s->state!=A733_IMX214_COLD_PARKED)
    return -EINVAL;
  memset(s,0,sizeof(*s));
  s->firmware_power_assumed=io->firmware_power!=NULL;
  s->started_ms=io->now_ms(io->context);
  s->stage=s->firmware_power_assumed ? "same-boot-firmware-power-assumption" : "live-AXP8191-power-read";
  int ret=power(s,io,s->power_before);
  if (ret) goto failed;
  s->stage="exclusive-unused-control-check";
  for (unsigned i=0;i<6 && !ret;++i)
    {ret=time_left(s,io);if (!ret) ret=rd(s,io,saved_regs[i],s->original+i);}
  if (ret) goto failed;
  s->snapshot_complete=true;
  if ((s->original[0]&0x0300001f)!=0 ||
      (s->original[1]&PIN_MASK)!=PIN_MASK || (s->original[2]&0xff)!=0xff ||
      (s->original[4]&0x80000000u) || s->original[5]!=0)
    {ret=-EBUSY;goto failed;}
  s->controls_owned=true;
  s->stage="assert-reset-and-standby";
  ret=update(s,io,PDATA,CONTROL_MASK,0);
  if (!ret) ret=update(s,io,CFG1,0xff,0x11);
  if (!ret) ret=delay(s,io,1);
  if (ret) goto failed;
  s->stage="configure-owned-MCLK1-and-TWI3";
  ret=update(s,io,MCLK1,MCLK_MASK,0); /* Gate stays OFF while dividers change. */
  if (!ret) ret=update(s,io,CFG0,PIN_MASK,0x00222000);
  if (!ret) ret=update(s,io,BGR3,0x10001,0x10001);
  if (!ret) s->twi_accessible=true;
  if (!ret) ret=wr(s,io,SRST,1);
  uint32_t poll_start=io->now_ms(io->context),value=1;
  while (!ret && (value&1))
    {
      if (++s->polls>4096 || io->now_ms(io->context)-poll_start>=10)
        {ret=-ETIMEDOUT;break;}
      ret=rd(s,io,SRST,&value);
    }
  if (!ret) ret=wr(s,io,CLK,0x59); /* sys24M/(10*2*(11+1))=100kHz */
  if (!ret) ret=rd(s,io,CLK,&value);
  if (!ret && (value&0x7f)!=0x59) ret=-EIO;
  if (!ret) ret=wr(s,io,CTL,0x48); /* Polling; no IRQ, START, STOP or DATA. */
  if (!ret) ret=rd(s,io,STAT,&value);
  if (!ret && (value&0xff)!=0xf8) ret=-EBUSY;
  if (!ret) ret=update(s,io,MCLK1,0x80000000u,0x80000000u);
  if (!ret) s->configured_mclk_hz=24000000;
  if (!ret) ret=delay(s,io,10);
  if (ret) goto failed;
  s->stage=s->firmware_power_assumed ? "power-NOT-resampled-firmware-retention-assumed" : "repeat-live-power-check";
  ret=s->firmware_power_assumed ? time_left(s,io) : power(s,io,s->power_after);
  if (ret) goto failed;
  s->state=A733_IMX214_COLD_PREPARED;
  s->stage="controls-prepared-reset-held-MCLK-NOT-measured";
  ret=time_left(s,io);
  if (!ret) return 0;
failed:
  s->error=ret;
  s->state=A733_IMX214_COLD_FAULT;
  if (s->controls_owned && s->dirty) (void)a733_imx214_cold_park(s,io);
  else s->controls_owned=false;
  s->elapsed_ms=io->now_ms(io->context)-s->started_ms;
  return ret;
}

int a733_imx214_cold_clock(struct a733_imx214_cold *s,
                          const struct a733_imx214_cold_io *io,bool calibration)
{
  if (!s || !valid(io)) return -EINVAL;
  if (s->state!=A733_IMX214_COLD_PREPARED || !s->controls_owned) return -EACCES;
  s->started_ms=io->now_ms(io->context);
  uint32_t cfg,data;
  int ret=rd(s,io,CFG1,&cfg);
  if (!ret) ret=rd(s,io,PDATA,&data);
  if (!ret && ((cfg&0xff)!=0x11 || (data&CONTROL_MASK))) ret=-EBUSY;
  if (!ret) ret=update(s,io,MCLK1,0x80000000u,0);
  if (!ret) s->configured_mclk_hz=0;
  if (!ret) ret=update(s,io,MCLK1,MCLK_MASK,calibration?23:0);
  if (!ret) ret=update(s,io,MCLK1,0x80000000u,0x80000000u);
  if (!ret) s->configured_mclk_hz=calibration?1000000:24000000;
  if (!ret) ret=delay(s,io,10);
  if (ret)
    {
      s->error=ret;s->state=A733_IMX214_COLD_FAULT;
      s->stage="clock-change-failed";
      (void)a733_imx214_cold_park(s,io);
    }
  else s->stage=calibration?"1MHz-calibration-output-NOT-measured":"24MHz-nominal-output-NOT-measured";
  s->elapsed_ms=io->now_ms(io->context)-s->started_ms;
  return ret;
}

struct trial_context
{
  struct a733_imx214_cold *state;
  const struct a733_imx214_cold_io *io;
};
static uint32_t trial_now(void *p)
{
  struct trial_context *c=p;
  return c->io->now_ms(c->io->context);
}
static int trial_read(void *p,uint32_t a,uint32_t *v)
{
  struct trial_context *c=p;
  return rd(c->state,c->io,a,v);
}
static int trial_write(void *p,uint32_t a,uint32_t v)
{
  struct trial_context *c=p;
  /* The experimental transport can write ONLY its fixed DATA/CTL window.
   * It never exposes arbitrary sensor addresses or CSI/DPHY/DMA registers.
   */
  if (a!=TWI3+8 && a!=CTL) return -EACCES;
  if (time_left(c->state,c->io)) return -ETIMEDOUT;
  c->state->last_address=a;++c->state->writes;
  uint32_t start=trial_now(p);
  int ret=result(c->io->write32(c->io->context,a,v,10));
  return !ret && trial_now(p)-start>10 ? -ETIMEDOUT : ret;
}

static int trial_mode_delay(void *p,unsigned ms)
{
  struct a733_imx214_twi3 *b=p;
  struct trial_context *c=b->context;
  return delay(c->state,c->io,ms);
}
static uint32_t trial_mode_now(void *p)
{
  struct a733_imx214_twi3 *b=p;
  return b->now_ms(b->context);
}
static int trial_common(struct a733_imx214_cold *s,
  const struct a733_imx214_cold_io *io,const void *blob,size_t bytes,uint32_t session_ms)
{
  if (!s || !valid(io)) return -EINVAL;
  if (s->id_trial_attempted) return -EALREADY;
  if (s->state!=A733_IMX214_COLD_PREPARED || !s->controls_owned ||
      !s->twi_accessible || s->configured_mclk_hz!=24000000) return -EACCES;
  s->id_trial_attempted=true;s->started_ms=io->now_ms(io->context);
  s->budget_ms=session_ms ? session_ms : blob ? 5000 : 250;
  s->mode_trial_attempted=blob!=NULL;
  s->stage="ID-experiment-preflight-NOT-electrical-qualification";
  /* Read back ALL owned configuration before releasing the module. These
   * values are configuration evidence only, never voltage/frequency proof.
   */
  const uint32_t masks[6]={0x0300001f,PIN_MASK,0xff,CONTROL_MASK,MCLK_MASK,0x10001};
  const uint32_t expected[6]={0,0x00222000,0x11,0,0x80000000,0x10001};
  int ret=0;uint32_t value;
  for (unsigned i=0;i<6 && !ret;++i)
    {
      ret=rd(s,io,saved_regs[i],&value);
      if (!ret && (value&masks[i])!=expected[i]) ret=-EACCES;
    }
  if (!ret) ret=rd(s,io,CLK,&value);
  if (!ret && (value&0x7f)!=0x59) ret=-EACCES;
  if (!ret) ret=rd(s,io,CTL,&value);
  if (!ret && (value&0x70)!=0x40) ret=-EBUSY;
  if (!ret) ret=rd(s,io,STAT,&value);
  if (!ret && (value&0xff)!=0xf8) ret=-EBUSY;
  /* Changed resources or failed preflight => no controller cleanup writes.
   * Ownership assumptions may no longer hold; only assert our controls.
   */
  bool preflight_failed=ret!=0;
  if (!ret) ret=update(s,io,PDATA,CONTROL_MASK,CONTROL_MASK);
  if (!ret) ret=delay(s,io,30);
  struct trial_context context={s,io};
  struct a733_imx214_twi3 bus={.read32=trial_read,.write32=trial_write,
    .now_ms=trial_now,.context=&context,.exclusive_sensor_and_bus=true};
  if (!ret) ret=a733_imx214_twi3_trial_id(&s->id_trial,&bus);
  if (!ret && blob)
    {
      struct a733_imx214_trial_mode_io mode_io={
        .read=a733_imx214_twi3_trial_read,.write8=a733_imx214_twi3_trial_write8,
        .delay_ms=trial_mode_delay,.now_ms=trial_mode_now,.context=&bus};
      ret=a733_imx214_mode_trial_prepare(&s->mode_trial,&mode_io,blob,bytes);
      if(!ret && io->packet_trial)ret=io->packet_trial(io->context,&bus);
    }
  s->id_polls=bus.polls;s->bus_quarantined=bus.quarantined || preflight_failed;
  if (ret && !s->id_trial.stage)
    {s->id_trial.error=ret;s->id_trial.stage="ID-experiment-not-transferred";}
  if (ret) {s->error=ret;s->state=A733_IMX214_COLD_FAULT;}
  if (s->bus_quarantined)
    {
      /* Do not STOP/reset/gate again after a failed STOP/arbitration loss.
       * Only assert the owned sensor controls; record readback failure.
       */
      s->cleaning=true;
      s->cleanup_error=update(s,io,PDATA,CONTROL_MASK,0);
      s->cleaning=false;
      s->stage="ID-experiment-bus-quarantined-reboot-required";
    }
  else
    {
      int cleanup=a733_imx214_cold_park(s,io);
      if (!ret) ret=cleanup;
      if (!s->cleanup_error)
        s->stage=ret ? "experiment-failed-controls-restored" : io->packet_trial ?
          "packet-trial-controls-restored-NOT-RAW-capture" : blob ?
          "mode-readback-passed-controls-restored-NOT-capture" :
          "ID-experiment-matched-controls-restored-NOT-capture";
    }
  s->elapsed_ms=io->now_ms(io->context)-s->started_ms;
  return ret;
}

int a733_imx214_cold_trial_id(struct a733_imx214_cold *s,
                             const struct a733_imx214_cold_io *io)
{return trial_common(s,io,NULL,0,0);}

int a733_imx214_cold_trial_mode(struct a733_imx214_cold *s,
  const struct a733_imx214_cold_io *io,const void *blob,size_t bytes)
{
  struct a733_imx214_mode mode;
  int ret=a733_imx214_mode_parse(blob,bytes,&mode);
  return ret ? ret : trial_common(s,io,blob,bytes,0);
}
int a733_imx214_cold_trial_session(struct a733_imx214_cold *s,
  const struct a733_imx214_cold_io *io,const void *blob,size_t bytes,uint32_t ms)
{
  if(!io || !io->packet_trial || ms<5000 || ms>3605000)return -EINVAL;
  struct a733_imx214_mode mode;int r=a733_imx214_mode_parse(blob,bytes,&mode);
  return r?r:trial_common(s,io,blob,bytes,ms);
}
