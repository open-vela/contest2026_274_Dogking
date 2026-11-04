/* SPDX-License-Identifier: Apache-2.0 */
/* Synthetic MMIO provider: no board, sensor, clock or power access. */
#include "a733_camera_board.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

struct fake
{
  uint32_t now;
  unsigned calls, fail_at, delay;
  int result;
};

static int read_reg(void *arg, uint32_t address, uint32_t *value,
                     unsigned timeout_ms)
{
  static const uint32_t allowed[]={0x02002518,0x02002e8c,0x02003804,
    0x02003840,0x02003844,0x02003860,0x020025e4,0x02000280,
    0x02000284,0x02000290,0x020002a0,0x020002b0};
  struct fake *f=arg;
  assert(f->calls < A733_CAMERA_BOARD_REGS);
  assert(address==allowed[f->calls]);
  assert(timeout_ms > 0 && timeout_ms <= 10);
  ++f->calls; f->now+=f->delay;
  if(f->calls==f->fail_at) return f->result;
  *value=address==0x02003804 ? 0x80000000 :
         address==0x02000280 ? 0x00222000 : 0;
  return 0;
}

static uint32_t now_ms(void *arg){return ((struct fake*)arg)->now;}

int main(void)
{
  const struct a733_camera_board_io io={read_reg,now_ms};
  struct a733_camera_board s;
  for(unsigned wrap=0;wrap<2;++wrap)
    {
      struct fake f={.now=wrap?UINT32_MAX-20:0,.delay=1};
      assert(a733_camera_board_sample(&s,&io,&f)==0);
      assert(s.complete && s.reads==12 && s.elapsed_ms==12);
      char report[2048];
      size_t n=a733_camera_board_report(&s,report,sizeof(report));
      assert(n>800 && n<2047 && strlen(report)==n);
      assert(strstr(report,"scl-mux=2 sda-mux=2 mclk-mux=2"));
      assert(strstr(report,"master-M=1 master-N=1 gate=1"));
      assert(strstr(report,"physical provider=unbound"));
      assert(strstr(report,"configuration matches NEVER unlock"));
      char small[8];assert(a733_camera_board_report(&s,small,8)==7);
      assert(small[7]==0);
    }
  struct a733_camera_board configured = {.complete=true,.reads=12};
  configured.raw[1]=0x10001; configured.raw[2]=0x80000000;
  configured.raw[7]=0x00222000; configured.raw[8]=0x11;
  char report[2048];
  a733_camera_board_report(&configured,report,sizeof(report));
  assert(strstr(report,"matched=6/6") && strstr(report,"rails=unknown"));
  configured.complete=false;
  a733_camera_board_report(&configured,report,sizeof(report));
  assert(strstr(report,"matched=0/6"));
  configured.complete=true;configured.error=-EIO;
  a733_camera_board_report(&configured,report,sizeof(report));
  assert(strstr(report,"matched=0/6"));
  for(unsigned i=1;i<=12;++i)
    for(unsigned positive=0;positive<2;++positive)
      {
        struct fake f={.fail_at=i,.result=positive?7:-EIO};
        assert(a733_camera_board_sample(&s,&io,&f)==(positive?-EPROTO:-EIO));
        assert(!s.complete && s.reads==i && f.calls==i);
      }
  struct fake slow={.delay=11};
  assert(a733_camera_board_sample(&s,&io,&slow)==-ETIMEDOUT);
  assert(slow.calls==1 && !s.complete);
  struct fake total={.delay=10};
  assert(a733_camera_board_sample(&s,&io,&total)==-ETIMEDOUT);
  assert(total.calls==10);
  assert(a733_camera_board_sample(&s,NULL,NULL)==-EINVAL);
  assert(a733_camera_board_sample(NULL,&io,NULL)==-EINVAL);
  puts("PASS: fixed camera CCU/PIO whitelist, decode, short reads, failures, deadlines/wrap; SYNTHETIC only");
  return 0;
}
