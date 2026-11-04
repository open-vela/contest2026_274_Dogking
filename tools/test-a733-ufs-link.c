/* SPDX-License-Identifier: Apache-2.0 */
/* Production link core with deterministic hostile MMIO; no hardware. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "a733_ufs_link.h"

struct fake
{
  struct a733_ufs_link_board board;
  uint32_t regs[256];
  uint16_t cr[65536];
  uint32_t us;
  uint32_t pending_us;
  uint32_t latency_us;
  unsigned int calls;
  unsigned int writes;
  unsigned int uics;
  unsigned int released;
  unsigned int fence;
  unsigned int fault_call;
  unsigned int fault_command;
  unsigned int fault_kind;
  unsigned int bridge_mode;
  unsigned int cr_address;
  uint32_t cr_value;
  uint32_t connection;
  uint32_t rate;
  unsigned int tail_writes;
  int qualification;
  int release_error;
  bool frozen;
  bool no_sram;
  bool no_hce;
  bool no_device;
  bool wrong_power;
  bool wrong_cport;
  bool long_byte;
  bool deadline_jump;
  uint32_t base_ms;
  bool positive_error;
  bool enabled_phy;
  bool saved_debug;
  bool clock_timer;
  unsigned int cport_step;
  bool fatal_final;
};

static struct fake *fresh(void)
{
  struct fake *f = calloc(1, sizeof(*f));
  assert(f);
  f->board.proofs = A733_UFS_LINK_PROOFS;
  f->regs[8 / 4] = 0x300;
  f->regs[0x200 / 4] = 0x130d;
  f->regs[0x204 / 4] = 0x800003ff;
  f->regs[0x30 / 4] = 8;
  f->cr[0xc002] = 0x5aa5;
  return f;
}

static int qualify(void *ctx, struct a733_ufs_link_board *b)
{
  struct fake *f = ctx;
  *b = f->board;
  return f->qualification;
}

static int read32(void *ctx, unsigned int off, uint32_t *v)
{
  struct fake *f = ctx;
  assert(off <= 0x32c && off % 4 == 0);
  if (++f->calls == f->fault_call) return f->positive_error ? 1 : -EIO;
  if (off == 0x20 && f->pending_us && f->us >= f->pending_us)
    {
      f->regs[0x20 / 4] |= 0x400;
      f->pending_us = 0;
    }
  *v = f->regs[off / 4];
  if (f->fatal_final && off == 0x9c && f->cport_step == 3 &&
      (f->regs[0x94 / 4] >> 16) == 0x4020)
    f->regs[0x20 / 4] |= 0x10000;
  return 0;
}

static int write32(void *ctx, unsigned int off, uint32_t v)
{
  struct fake *f = ctx;
  unsigned int attr;
  unsigned int selector;
  uint32_t value;
  assert(off == 0x34 || off == 0x20 || off == 0x94 || off == 0x98 ||
         off == 0x9c || off == 0x90 || off == 0x200);
  if (++f->calls == f->fault_call) return f->positive_error ? 1 : -EIO;
  ++f->writes;
  if (f->fault_command && f->uics >= f->fault_command) ++f->tail_writes;
  if (off == 0x20)
    {
      assert(v == 0x400); /* Never clears fatal or transfer-completion bits. */
      f->regs[off / 4] &= ~v;
      return 0;
    }
  if (off == 0x34 && f->no_hce) return 0;
  if (off == 0x200)
    assert(v == (f->regs[off / 4] | (1u << 19)));
  f->regs[off / 4] = v;
  if (off != 0x90) return 0;
  assert(v == 1 || v == 2 || v == 0x16);
  assert(f->regs[0x34 / 4] == 1);
  ++f->uics;
  if (f->uics == f->fault_command)
    {
      if (f->fault_kind == 1) return 0; /* UIC never completes. */
      if (f->fault_kind == 2)
        {
          f->regs[0x98 / 4] = 1;
          f->regs[0x20 / 4] |= 0x400;
          return 0;
        }
      f->regs[0x20 / 4] |= 0x10000;
      return 0;
    }
  if (v == 0x16)
    {
      assert(f->released && f->enabled_phy && f->saved_debug && f->clock_timer &&
             (f->regs[0x200 / 4] & (1u << 19)));
      f->regs[0x30 / 4] = f->no_device ? 8 : 0xf;
    }
  attr = f->regs[0x94 / 4] >> 16;
  selector = f->regs[0x94 / 4] & 65535;
  value = f->regs[0x9c / 4];
  if (v == 2)
    {
      if (attr == 0x8114) f->rate = value;
      if (attr == 0x8132) assert(value == 0x90);
      if (attr == 0x811f) assert(value == 1);
      if (attr == 0xd0c1) {assert(value == 0); f->enabled_phy = true;}
      if (attr == 0xd0a0) {assert(value == 0x1b && f->enabled_phy); f->saved_debug = true;}
      if (attr == 0xd0fb) {assert(value == 10 && f->saved_debug); f->clock_timer = true;}
      if (attr == 0x8116) f->cr_address = (f->cr_address & 0xff00) | value;
      if (attr == 0x8117) f->cr_address = (f->cr_address & 255) | (value << 8);
      if (attr == 0x8118) f->cr_value = (f->cr_value & 0xff00) | value;
      if (attr == 0x8119) f->cr_value = (f->cr_value & 255) | (value << 8);
      if (attr == 0x811c) f->bridge_mode = value;
      if (attr == 0xd085 && f->bridge_mode == 1) f->cr[f->cr_address] = f->cr_value;
      if (attr == 0x4020)
        {
          if (!value) {assert(f->cport_step == 0); f->cport_step = 1;}
          else {assert(value == 1 && f->cport_step == 2); f->cport_step = 3;}
          f->connection = value;
        }
      if (attr == 0x4025) {assert(value == 6 && f->cport_step == 1); f->cport_step = 2;}
      if (attr == 0x8009 || attr == 0x8013) assert(selector == 4 || selector == 5);
    }
  if (v == 1)
    {
      if (attr == 0x811a) value = f->long_byte ? 256 : f->cr[f->cr_address] & 255;
      else if (attr == 0x811b) value = f->cr[f->cr_address] >> 8;
      else if (attr == 0xd083) value = f->wrong_power ? 1 : 2;
      else if (attr == 0x4020) value = f->wrong_cport ? 0 : f->connection;
      else assert(!"Unexpected DME get");
      f->regs[0x9c / 4] = value;
    }
  if (f->latency_us) f->pending_us = f->us + f->latency_us;
  else f->regs[0x20 / 4] |= 0x400;
  return 0;
}

static int release_phy(void *ctx)
{
  struct fake *f = ctx;
  ++f->released;
  assert(f->uics == 10);
  if (!f->no_sram) f->regs[0x200 / 4] |= 1u << 24;
  return f->release_error;
}

static void fence(void *ctx) {++((struct fake *)ctx)->fence;}
static void delay(void *ctx, unsigned int us)
{
  struct fake *f = ctx;
  assert(us == 100 || us == 1000);
  f->us += us;
  assert(f->us <= 10000000);
}
static uint32_t now(void *ctx)
{
  struct fake *f = ctx;
  return f->frozen ? 0 : f->base_ms + f->us / 1000 +
                         (f->deadline_jump && f->uics >= 25 ? 10001 : 0);
}

static int run(struct fake *f, struct a733_ufs_link_state *s)
{
  struct a733_ufs_link_io io = {f, qualify, read32, write32, release_phy,
                                fence, delay, now};
  return a733_ufs_link_start(s, &io);
}

static void failed(struct fake *f, struct a733_ufs_link_state *s)
{
  unsigned int calls = f->calls;
  unsigned int writes = f->writes;
  assert(s->attempted && s->quarantined && !s->link_up && s->error < 0);
  assert(run(f, s) == -EIO && f->calls == calls && f->writes == writes);
  assert(f->fence > 0);
}

static void profile(struct fake *f, unsigned int p)
{
  if (p)
    {
      f->board.cal_low = 0x22110000;
      f->board.cal_high = p == 1 ? 0x00203344 : 0x50203344;
    }
}

int main(void)
{
  unsigned int i;
  unsigned int count;
  unsigned int io_count;
  unsigned int fault_points = 0;
  struct a733_ufs_link_state s = {0};
  struct fake *f = fresh();
  assert(run(f, &s) == 0 && s.link_up && !s.quarantined && s.attempted);
  assert(s.phase == A733_UFS_LINK_UP && f->released == 1 && f->rate == 1);
  assert(f->cr[0xc002] == 0x5aa5 && f->cr[0x401c] == 4 &&
         f->cr[0x411c] == 4 && f->cr[0x20] == 8);
  count = f->uics;
  io_count = f->calls;
  assert(count == 50 && run(f, &s) == -EALREADY && f->calls == io_count);
  free(f);
  assert(a733_ufs_link_start(NULL, NULL) == -EINVAL);
  for (i = 0; i < 9; i++)
    {
      f = fresh(); memset(&s, 0, sizeof(s));
      f->board.proofs &= ~(1u << i);
      assert(run(f, &s) == -ENOTSUP && !f->calls && !f->writes && !s.attempted);
      free(f);
    }
  for (i = 0; i < 6; i++)
    {
      f = fresh(); memset(&s, 0, sizeof(s));
      if (i == 0) f->qualification = -EAGAIN;
      if (i == 1) f->qualification = 1;
      if (i == 2) f->board.cal_high = 0x01000000;
      if (i == 3) f->board.cal_high = 0x00130000;
      if (i == 4) f->board.cal_low = 0x00800000;
      if (i == 5) f->board.cal_low = 0xffff0000;
      assert(run(f, &s) < 0 && !f->calls && !f->writes && !s.attempted);
      free(f);
    }
  for (i = 0; i < 7; i++)
    {
      static const unsigned int busy[] = {0x34, 0x24, 0x18, 0x58, 0x78, 0x60, 0x80};
      f = fresh(); memset(&s, 0, sizeof(s)); f->regs[busy[i] / 4] = 1;
      assert(run(f, &s) == -EBUSY && !f->writes && !s.attempted);
      free(f);
    }
  for (i = 0; i < 5; i++)
    {
      f = fresh(); memset(&s, 0, sizeof(s));
      if (i == 0) f->regs[8 / 4] = 0xffffffff;
      if (i == 1) f->regs[0x200 / 4] |= 1u << 20;
      if (i == 2) f->regs[0x204 / 4] |= 1u << 16;
      if (i == 3) f->regs[0x32c / 4] = 1;
      if (i == 4) f->regs[0x20 / 4] = 0x10000;
      assert(run(f, &s) < 0 && !f->writes && !s.attempted);
      free(f);
    }
  for (i = 0; i < 10; i++)
    {
      f = fresh(); memset(&s, 0, sizeof(s));
      if (i == 0) f->no_hce = true;
      if (i == 1) f->no_sram = true;
      if (i == 2) f->release_error = -EIO;
      if (i == 3) f->no_device = true;
      if (i == 4) f->wrong_power = true;
      if (i == 5) f->wrong_cport = true;
      if (i == 6) f->long_byte = true;
      if (i == 7) {f->regs[0x30 / 4] = 0; f->frozen = true;}
      if (i == 8) f->release_error = 1;
      if (i == 9) f->fatal_final = true;
      assert(run(f, &s) < 0); failed(f, &s); free(f);
    }
  for (unsigned int p = 0; p < 3; p++)
    {
      f = fresh(); memset(&s, 0, sizeof(s)); profile(f, p);
      assert(run(f, &s) == 0);
      unsigned int pn = f->uics;
      unsigned int pi = f->calls;
      assert(pn == (p ? 92 : 50));
      fault_points += pn * 3 + pi;
      free(f);
      /* Every UIC result/fatal/timeout, in all calibration branches. */
      for (i = 1; i <= pn; i++)
        for (unsigned int kind = 1; kind <= 3; kind++)
          {
            f = fresh(); memset(&s, 0, sizeof(s)); profile(f, p);
            f->fault_command = i; f->fault_kind = kind;
            assert(run(f, &s) < 0 && f->uics == i && !f->tail_writes);
            failed(f, &s); free(f);
          }
      /* Every normal-path MMIO read/write error must end the attempt. */
      for (i = 1; i <= pi; i++)
        {
          f = fresh(); memset(&s, 0, sizeof(s)); profile(f, p); f->fault_call = i;
          assert(run(f, &s) < 0 && f->calls == i);
          if (s.attempted) failed(f, &s); else assert(!f->writes);
          free(f);
        }
    }
  for (i = 1; i <= io_count; i++)
      {
        f = fresh(); memset(&s, 0, sizeof(s));
        f->fault_call = i; f->positive_error = true;
        assert(run(f, &s) == -EPROTO && f->calls == i);
        if (s.attempted) failed(f, &s); else assert(!f->writes);
        free(f);
      }
  for (i = 0; i < 2; i++)
    {
      f = fresh(); memset(&s, 0, sizeof(s));
      f->board.cal_low = 0x22110000;
      f->board.cal_high = i ? 0x50203344 : 0x00203344;
      assert(run(f, &s) == 0 && f->rate == i);
      assert(f->cr[0x7014] == (i ? 0x50 : 0x20));
      assert(f->cr[0x4000] == 0x11 && f->cr[0x4001] == 0x22 &&
             f->cr[0x4100] == 0x44 && f->cr[0x4101] == 0x33 &&
             f->cr[0x401e] == 0xa00 && f->cr[0x411e] == 0xa00);
      free(f);
    }
  /* Whole-operation deadline, independent of each command's 100ms limit. */
  f = fresh(); memset(&s, 0, sizeof(s)); f->deadline_jump = true;
  assert(run(f, &s) == -ETIMEDOUT && f->us <= 10000000);
  failed(f, &s); free(f);
  f = fresh(); memset(&s, 0, sizeof(s)); f->base_ms = UINT32_MAX;
  assert(run(f, &s) == 0 && s.elapsed_ms == 1); free(f);
  f = fresh(); memset(&s, 0, sizeof(s)); f->latency_us = 500;
  assert(run(f, &s) == 0 && s.polls > 10); free(f);
  printf("UFS link core PASS: default %u UIC/%u MMIO; %u fault points across 3 calibration branches + %u positive-error points; NOT hardware\n", count, io_count, fault_points, io_count);
  return 0;
}
