/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "a733_ufs_board.h"

static const uint32_t addresses[] =
{0x02002000,0x020020a0,0x020020a8,0x020020ac,0x020025c0,
 0x020025e0,0x0200259c,0x02002d80,0x02002d84,0x02002d8c,
 0x02002d90,0x07090160,0x0709016c};
struct fixture {unsigned calls,fail,change;int error;uint32_t value[13];};
static int read_fixture(void *context,uint32_t address,uint32_t *value)
{
  struct fixture *f=context;unsigned i=f->calls%13;
  assert(address==addresses[i]);f->calls++;
  if(f->fail==f->calls)return f->error;
  *value=f->value[i];
  if(f->change==f->calls)*value^=1;
  return 0;
}
static void init(struct fixture *f)
{
  memset(f,0,sizeof(*f));
  f->value[4]=1u<<24;f->value[5]=1u<<30;f->value[6]=0x30001;
  f->value[7]=0x80000000;f->value[8]=0x80000018;f->value[9]=0xf0001;
  f->value[10]=1;f->value[11]=2u<<14;f->value[12]=1;
}
int main(void)
{
  struct a733_ufs_board s={0};struct fixture f;char text[2048];
  assert(a733_ufs_board_sample(NULL,read_fixture,&f)==-EINVAL);
  assert(a733_ufs_board_sample(&s,NULL,&f)==-EINVAL&&!s.attempts);
  assert(!a733_ufs_board_hci_allowed());
  for(unsigned p=0;p<4;p++){
    init(&f);f.value[11]=p<<14;
    assert(!a733_ufs_board_sample(&s,read_fixture,&f));
    assert(f.calls==26&&s.reads==26&&s.stable&&s.gates==127&&s.resets==15);
    assert(s.axi_mux==0&&s.axi_div==1&&s.cfg_mux==0&&s.cfg_div==25);
    assert(s.dcxo_hz==(p==1?19200000u:p==2?26000000u:24000000u));
    assert(!a733_ufs_board_hci_allowed());
  }
  for(unsigned point=1;point<=26;point++)for(unsigned e=0;e<2;e++){
    init(&f);f.fail=point;f.error=e?1:-EIO;
    assert(a733_ufs_board_sample(&s,read_fixture,&f)==(e?-EPROTO:-EIO));
    assert(f.calls==point&&s.reads==point&&!s.stable);
    assert(s.last_address==addresses[(point-1)%13]);
    assert(!a733_ufs_board_hci_allowed());
  }
  for(unsigned point=14;point<=26;point++){
    init(&f);f.change=point;
    assert(a733_ufs_board_sample(&s,read_fixture,&f)==-EAGAIN);
    assert(f.calls==point&&!s.stable&&!a733_ufs_board_hci_allowed());
  }
  init(&f);f.value[7]=0x8700001f;f.value[8]=0x8700001f;
  s.attempts=UINT32_MAX;
  assert(!a733_ufs_board_sample(&s,read_fixture,&f));
  assert(s.attempts==UINT32_MAX&&s.axi_mux==7&&s.cfg_mux==7&&s.axi_div==32);
  assert(!a733_ufs_board_hci_allowed());
  assert(a733_ufs_board_report(&s,text,sizeof(text))==strlen(text));
  assert(strstr(text,"hci-access=blocked")&&strstr(text,"NOT measured"));
  assert(a733_ufs_board_report(&s,text,1)==0&&text[0]==0);
  assert(a733_ufs_board_report(&s,text,8)==7&&text[7]==0);
  assert(!a733_ufs_board_report(NULL,text,sizeof(text)));
  assert(!a733_ufs_board_report(&s,NULL,sizeof(text)));
  assert(!a733_ufs_board_report(&s,text,0));
  memset(&s,0,sizeof(s));
  assert(a733_ufs_board_report(&s,text,sizeof(text))&&strstr(text,"not-sampled"));
  puts("UFS board production sampler: 65 read/change fault points, selectors, saturation, report bounds PASS; no hardware");
}
