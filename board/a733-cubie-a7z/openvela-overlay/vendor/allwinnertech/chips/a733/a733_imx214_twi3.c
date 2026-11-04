/* SPDX-License-Identifier: Apache-2.0 */
#include "a733_imx214_twi3.h"
#include <errno.h>
#include <string.h>

#define TWI3 0x02513000u
#define DATA 0x08u
#define CTL  0x0cu
#define STAT 0x10u
#define CLK  0x14u
#define EN   0x40u
#define STA  0x20u
#define STP  0x10u
#define IFLG 0x08u
#define ACK  0x04u

struct transfer
{
  struct a733_imx214_twi3 *bus;
  uint32_t start;
  unsigned budget;
  unsigned left;
};

static int rd(struct a733_imx214_twi3 *b, uint32_t address, uint32_t *value)
{
  int ret = b->read32(b->context, address, value);
  if (ret > 0) ret = -EPROTO;
  if (ret) b->quarantined = true;
  return ret;
}

static int wr(struct a733_imx214_twi3 *b, uint32_t offset, uint32_t value)
{
  /* The only write space is TWI3 DATA/CTL. No SRST, clock, GPIO, other
   * buses, CCI hardware engine or CSI/ISP CPU-register writes. Sensor
   * register transactions use only this DATA/CTL window after strict ID.
   */
  if (offset != DATA && offset != CTL) return -EACCES;
  int ret = b->write32(b->context, TWI3 + offset, value);
  if (ret > 0) ret = -EPROTO;
  if (ret) b->quarantined = true;
  return ret;
}

static int wait_flag(struct transfer *t, uint32_t mask, bool set,
                      bool stopping)
{
  struct a733_imx214_twi3 *b = t->bus;
  for (;;)
    {
      /* Reserve 10ms of the one deadline for STOP. Poll-count cap also
       * prevents an infinite loop if the supplied monotonic clock stalls.
       */
      unsigned limit = stopping ? t->budget : t->budget - 10;
      if (!t->left || b->now_ms(b->context) - t->start >= limit)
        return -ETIMEDOUT;
      --t->left;
      ++b->polls;
      uint32_t ctl;
      int ret = rd(b, TWI3 + CTL, &ctl);
      if (ret) return ret;
      if (((ctl & mask) != 0) == set) return 0;
    }
}

static int step(struct transfer *t, uint32_t command, uint32_t expected)
{
  if (t->bus->now_ms(t->bus->context) - t->start >= t->budget - 10)
    return -ETIMEDOUT;
  int ret = wr(t->bus, CTL, command);
  if (ret) return ret;
  ret = wait_flag(t, IFLG, true, false);
  if (ret) return ret;
  uint32_t status;
  ret = rd(t->bus, TWI3 + STAT, &status);
  if (ret) return ret;
  status &= 255;
  if (status == expected) return 0;
  if (status == 0x38 || status == 0x00)
    {
      /* Arbitration lost/bus error: never drive STOP into a new master's
       * transfer. Latch ownership uncertainty instead of resetting/retrying.
       */
      t->bus->quarantined = true;
      return -EIO;
    }
  return status == 0x20 || status == 0x48 ? -ENXIO : -EIO;
}

static int byte(struct transfer *t, uint8_t value, uint32_t status)
{
  if (t->bus->now_ms(t->bus->context) - t->start >= t->budget - 10)
    return -ETIMEDOUT;
  int ret = wr(t->bus, DATA, value);
  return ret ? ret : step(t, EN | IFLG, status);
}

static int preflight(struct a733_imx214_twi3 *b)
{
  uint32_t bgr, cfg0, cfg1, data, master, apb, clock;
  int ret = rd(b, 0x02002e8c, &bgr);
  if (!ret) ret = rd(b, 0x02000280, &cfg0);
  if (!ret) ret = rd(b, 0x02000284, &cfg1);
  if (!ret) ret = rd(b, 0x02000290, &data);
  if (!ret) ret = rd(b, 0x02003804, &master);
  if (!ret) ret = rd(b, 0x02002518, &apb);
  if (ret) return ret;
  /* Confirm gate/reset BEFORE accessing the TWI module. This narrow
   * handoff contract accepts only sys24M /1 master1 and APB1 sys24M /1.
   * Other valid configurations need independent qualification, not guesses.
   */
  if ((bgr & 0x10001) != 0x10001 ||
      ((cfg0 >> 12) & 0xfff) != 0x222 ||
      (cfg1 & 0xff) != 0x11 || (data & 0x300) != 0x300 ||
      (master & 0x87001f1f) != 0x80000000 ||
      (apb & 0x0300001f) != 0)
    return -EACCES;
  ret = rd(b, TWI3 + CLK, &clock);
  if (ret) return ret;
  unsigned frequency = 24000000u /
    (10u * (1u << (clock & 7)) * (((clock >> 3) & 15) + 1));
  return frequency <= 100000u ? 0 : -ERANGE;
}

static uint32_t milliseconds(void *context)
{
  struct a733_imx214_twi3 *b = context;
  return b->now_ms(b->context);
}

static int finish(struct transfer *t, bool started, int result)
{
  struct a733_imx214_twi3 *b = t->bus;
  if (started && !b->quarantined)
    {
      int stop = wr(b, CTL, EN | STP | IFLG);
      if (!stop) stop = wait_flag(t, STP, false, true);
      if (stop) {b->quarantined = true; if (!result) result = stop;}
    }
  if (!result && b->now_ms(b->context) - t->start > t->budget)
    result = -ETIMEDOUT;
  if (result == -ETIMEDOUT) b->quarantined = true;
  return result;
}

static int read_sensor(void *context, uint8_t address, uint16_t reg,
                        uint8_t *data, size_t count, unsigned timeout_ms)
{
  struct a733_imx214_twi3 *b = context;
  if (address != 0x10 || !data || !count || count > 4 ||
      timeout_ms > 100 || b->quarantined)
    return -EINVAL;
  if (timeout_ms <= 10) return -ETIMEDOUT;
  struct transfer t = {.bus = b, .start = b->now_ms(b->context),
                       .budget = timeout_ms, .left = 100000};
  int ret = preflight(b);
  if (ret) return ret;
  if (b->now_ms(b->context) - t.start >= t.budget - 10)
    return -ETIMEDOUT;
  bool started = false;
  uint32_t ctl;
  ret = rd(b, TWI3 + CTL, &ctl);
  if (ret) return ret;
  if ((ctl & (EN | STA | STP)) != EN) return -EBUSY;
  /* Require idle instead of taking an existing owner's active transfer. */
  uint32_t status;
  ret = rd(b, TWI3 + STAT, &status);
  if (ret) return ret;
  if ((status & 255) != 0xf8) return -EBUSY;
  started = true;
  ret = step(&t, EN | STA | IFLG, 0x08);
  if (!ret) ret = byte(&t, 0x20, 0x18);
  if (!ret) ret = byte(&t, (uint8_t)(reg >> 8), 0x28);
  if (!ret) ret = byte(&t, (uint8_t)reg, 0x28);
  if (!ret) ret = step(&t, EN | STA | IFLG, 0x10);
  if (!ret) ret = byte(&t, 0x21, 0x40);
  for (size_t i = 0; !ret && i < count; ++i)
    {
      bool ack = i + 1 < count;
      ret = step(&t, EN | IFLG | (ack ? ACK : 0), ack ? 0x50 : 0x58);
      uint32_t value;
      if (!ret) ret = rd(b, TWI3 + DATA, &value);
      if (!ret) data[i] = (uint8_t)value;
    }
  return finish(&t, started, ret);
}

int a733_imx214_twi3_identify(struct a733_imx214_probe *out,
                             struct a733_imx214_twi3 *b)
{
  if (!out) return -EINVAL;
  memset(out, 0, sizeof(*out));
  out->stage = "v11-twi3-power-clock-ownership-unqualified";
  out->error = -EACCES;
  if (!b || !b->read32 || !b->write32 || !b->now_ms)
    return out->error = -EINVAL;
  b->identity_verified = false;
  b->trial_identity_match = false;
  if (!b->board_power_qualified || !b->exclusive_sensor_and_bus ||
      b->measured_mclk_hz != 24000000 || b->quarantined)
    return out->error;
  struct a733_imx214_io io =
  {
    .read = read_sensor, .now_ms = milliseconds,
    .profile = A733_CAMERA_A7Z_V11_SINGLE,
    .board_power_qualified = true, .measured_mclk_hz = 24000000
  };
  int ret = a733_imx214_identify(out, &io, b);
  /* The outer probe also detects callbacks which returned after their
   * allowance or the aggregate deadline. Preserve that fault here too,
   * even if STOP looked complete before the callback returned late.
   */
  if (ret == -ETIMEDOUT) b->quarantined = true;
  if (!ret) b->identity_verified = true;
  return ret;
}

int a733_imx214_twi3_trial_id(struct a733_imx214_probe *out,
                             struct a733_imx214_twi3 *b)
{
  if (!out) return -EINVAL;
  memset(out, 0, sizeof(*out));
  out->stage = "ID-experiment-NOT-electrical-qualification";
  out->error = -EACCES;
  if (!b || !b->read32 || !b->write32 || !b->now_ms)
    return out->error = -EINVAL;
  b->identity_verified = false;
  b->trial_identity_match = false;
  if (b->trial_attempted) return out->error = -EALREADY;
  if (!b->exclusive_sensor_and_bus || b->quarantined) return out->error;
  b->trial_attempted = true;
  uint32_t start = b->now_ms(b->context);
  uint8_t data[2];
  out->reads = 1;
  int ret = read_sensor(b, 0x10, 0x0016, data, sizeof(data), 100);
  if (!ret && b->now_ms(b->context) - start > 100) ret = -ETIMEDOUT;
  if (ret == -ETIMEDOUT) b->quarantined = true;
  if (!ret)
    {
      out->chip_id = (uint16_t)((unsigned)data[0] << 8 | data[1]);
      out->identity_match = out->chip_id == 0x0214;
      if (!out->identity_match) ret = -ENODEV;
    }
  out->stage = ret ? "ID-experiment-failed" : "ID-experiment-matched-NOT-capture";
  out->elapsed_ms = b->now_ms(b->context) - start;
  /* No full sensor snapshot and NO strict identity/qualification lease. */
  b->trial_identity_match=!ret;
  return out->error = ret;
}

static int session_gate(struct a733_imx214_twi3 *b)
{
  if (!b || !b->read32 || !b->write32 || !b->now_ms) return -EINVAL;
  if (!b->board_power_qualified || !b->exclusive_sensor_and_bus ||
      b->measured_mclk_hz != 24000000 || b->quarantined)
    {b->identity_verified = false; return -EACCES;}
  return b->identity_verified ? 0 : -EACCES;
}

int a733_imx214_twi3_read_bytes(void *context, uint16_t reg, uint8_t *data,
                                size_t count, unsigned timeout_ms)
{
  struct a733_imx214_twi3 *b = context;
  int ret = session_gate(b);
  if (ret) return ret;
  if ((!data || !count || count > 4 || (uint32_t)reg + count > 65536) ||
      (reg < 0x0100 && !(reg == 0x0016 && count == 2))) return -EINVAL;
  ret = read_sensor(b, 0x10, reg, data, count, timeout_ms);
  if (ret) b->identity_verified = false;
  return ret;
}

static int write_register(struct a733_imx214_twi3 *b,uint16_t reg,uint8_t value,
                           unsigned timeout_ms)
{
  int ret;
  if (reg < 0x0100 || reg == 0x0103 || reg == 0xffff ||
      (reg == 0x0100 && value > 1) || timeout_ms > 100) return -EACCES;
  if (timeout_ms <= 10) {b->identity_verified = false; return -ETIMEDOUT;}
  struct transfer t = {.bus = b, .start = b->now_ms(b->context),
                       .budget = timeout_ms, .left = 100000};
  ret = preflight(b);
  if (ret) {b->identity_verified = false; return ret;}
  if (b->now_ms(b->context) - t.start >= t.budget - 10)
    {b->identity_verified = false; b->quarantined = true; return -ETIMEDOUT;}
  uint32_t ctl, status;
  ret = rd(b, TWI3 + CTL, &ctl);
  if (!ret) ret = rd(b, TWI3 + STAT, &status);
  if (ret) {b->identity_verified = false; return ret;}
  if ((ctl & (EN | STA | STP)) != EN || (status & 255) != 0xf8)
    {b->identity_verified = false; return -EBUSY;}
  ret = step(&t, EN | STA | IFLG, 0x08);
  if (!ret) ret = byte(&t, 0x20, 0x18);
  if (!ret) ret = byte(&t, (uint8_t)(reg >> 8), 0x28);
  if (!ret) ret = byte(&t, (uint8_t)reg, 0x28);
  if (!ret) ret = byte(&t, value, 0x28);
  ret = finish(&t, true, ret);
  if (ret) b->identity_verified = false;
  return ret;
}

int a733_imx214_twi3_write8(void *context,uint16_t reg,uint8_t value,unsigned ms)
{
  struct a733_imx214_twi3 *b=context;
  int ret=session_gate(b);
  return ret ? ret : write_register(b,reg,value,ms);
}

static int trial_gate(struct a733_imx214_twi3 *b)
{
  if (!b || !b->read32 || !b->write32 || !b->now_ms) return -EINVAL;
  if (!b->exclusive_sensor_and_bus || b->quarantined ||
      !b->trial_attempted || !b->trial_identity_match) return -EACCES;
  return 0;
}
int a733_imx214_twi3_trial_read(void *p,uint16_t reg,uint8_t *data,size_t n,unsigned ms)
{
  struct a733_imx214_twi3 *b=p;
  int ret=trial_gate(b);
  if (ret) return ret;
  if (!data || !n || n>4 || (uint32_t)reg+n>65536 ||
      (reg<0x100 && !(reg==0x16 && n==2))) return -EINVAL;
  ret=read_sensor(b,0x10,reg,data,n,ms);
  if (ret==-EACCES || ret==-EBUSY) b->quarantined=true;
  if (ret) b->trial_identity_match=false;
  return ret;
}
int a733_imx214_twi3_trial_write8(void *p,uint16_t reg,uint8_t value,unsigned ms)
{
  struct a733_imx214_twi3 *b=p;
  int ret=trial_gate(b);
  if (ret) return ret;
  if (reg==0x0100 && value!=0) return -EACCES; /* No experimental STREAM_ON. */
  ret=write_register(b,reg,value,ms);
  if (ret==-EACCES || ret==-EBUSY) b->quarantined=true;
  if (ret) b->trial_identity_match=false;
  return ret;
}

int a733_imx214_twi3_packet_stream(struct a733_imx214_twi3 *b,
  int (*receiver_check)(void *),void *receiver,bool on)
{
  if(!b || !b->read32 || !b->write32 || !b->now_ms)return -EINVAL;
  if(!b->exclusive_sensor_and_bus || b->quarantined)return -EACCES;
  int ret=0;
  if(on)
    {
      ret=trial_gate(b);
      if(!ret && b->packet_stream_attempted)ret=-EALREADY;
      if(!ret && (!receiver_check || !receiver))ret=-EACCES;
      if(!ret)ret=receiver_check(receiver);
      if(ret)return ret>0 ? -EPROTO : ret;
      b->packet_stream_attempted=true;
    }
  else if(!b->packet_stream_attempted)return -EACCES;
  ret=write_register(b,0x0100,on?1:0,30);
  uint8_t value=255;
  if(!ret)ret=read_sensor(b,0x10,0x0100,&value,1,30);
  if(!ret && value!=(on?1:0))ret=-EIO;
  if(ret==-EACCES || ret==-EBUSY) b->quarantined=true;
  if(ret)b->trial_identity_match=false;
  return ret;
}
