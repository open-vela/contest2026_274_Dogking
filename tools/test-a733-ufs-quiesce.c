/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "a733_ufs_quiesce.h"
struct fixture
{
  uint32_t regs[33];
  unsigned reads, writes, fail_read, fail_write, lag, pending, step;
  uint32_t us;
  bool frozen, jump, mutate;
  int fault;
};
static int rd(void *ctx, uint32_t off, uint32_t *value)
{
  struct fixture *f=ctx;
  ++f->reads;
  assert(!(off&3) && off<=0x80);
  if(f->reads==f->fail_read)return f->fault;
  if(f->pending!=0 && f->pending==off && f->lag){
    --f->lag;
    if(!f->lag){f->regs[off/4]=0;f->pending=0;}
  }
  *value=f->regs[off/4];
  return 0;
}
static int wr(void *ctx, uint32_t off)
{
  struct fixture *f=ctx;
  static const uint32_t expected[]={0x24,0x60,0x80};
  assert(f->writes<3 && off==expected[f->writes]);
  ++f->writes;
  if(f->lag)f->pending=off;
  else f->regs[off/4]=0;
  if(f->mutate)f->regs[0x50/4]^=0x400;
  return f->writes==f->fail_write?f->fault:0;
}
static uint32_t now(void *ctx)
{
  struct fixture *f=ctx;
  if(f->jump)return f->step++?21:0;
  return f->us/1000;
}
static void delay(void *ctx)
{
  struct fixture *f=ctx;
  if(!f->frozen)f->us+=100;
}
static const struct a733_ufs_quiesce_io io={rd,wr,now,delay};
static void init(struct fixture *f,struct a733_ufs_board *b,
                 struct a733_ufs_quiesce *s)
{
  memset(f,0,sizeof(*f));memset(b,0,sizeof(*b));memset(s,0,sizeof(*s));
  b->stable=true;b->reads=26;b->gates=127;b->resets=15;b->dcxo_hz=26000000;
  b->raw[0]=0xf8675f00;b->raw[1]=0xff126310;b->raw[6]=0x30001;
  b->raw[7]=0x81000000;b->raw[8]=0x80000018;b->raw[9]=0xf0001;
  b->raw[11]=0x183fb0f7;b->raw[12]=0x511;
  f->regs[0]=0x11870f1f;f->regs[2]=0x300;f->regs[0x30/4]=0x10f;
  f->regs[0x34/4]=1;f->regs[0x24/4]=0x30e15;
  f->regs[0x50/4]=0xbfeb4c00;f->regs[0x60/4]=1;f->regs[0x80/4]=1;
}
int main(void)
{
  struct fixture f;struct a733_ufs_board b;struct a733_ufs_quiesce s;
  char report[2048];
  init(&f,&b,&s);
  assert(a733_ufs_quiesce_run(NULL,&b,&io,&f)==-EINVAL);
  assert(a733_ufs_quiesce_run(&s,&b,NULL,&f)==-EINVAL);
  struct a733_ufs_quiesce_io bad=io;bad.write_zero=NULL;
  assert(a733_ufs_quiesce_run(&s,&b,&bad,&f)==-EINVAL&&!s.attempted);
  assert(!a733_ufs_quiesce_run(&s,&b,&io,&f));
  assert(s.completed&&!s.quarantine&&!s.error&&s.writes==3&&f.writes==3);
  assert(f.reads==227&&s.polls==3&&s.after.raw[8]==0xbfeb4c00);
  unsigned success_reads=f.reads;
  assert(a733_ufs_quiesce_run(&s,&b,&io,&f)==-EALREADY&&s.completed);
  assert(f.reads==success_reads&&f.writes==3&&!s.error);
  assert(!a733_ufs_board_hci_allowed());
  assert(a733_ufs_quiesce_report(&s,report,sizeof(report))==strlen(report));
  assert(strstr(report,"completed=1")&&strstr(report,"DMA/attach blocked"));
  for(unsigned size=1;size<sizeof(report);size++){
    memset(report,0xa5,sizeof(report));
    size_t n=a733_ufs_quiesce_report(&s,report,size);
    assert(n<size&&report[n]==0&&(unsigned char)report[size]==0xa5);
  }
  assert(!a733_ufs_quiesce_report(&s,NULL,10));
  assert(!a733_ufs_quiesce_report(NULL,report,10));
  for(unsigned point=1;point<=success_reads;point++)for(unsigned positive=0;positive<2;positive++){
    init(&f,&b,&s);f.fail_read=point;f.fault=positive?7:-EIO;
    assert(a733_ufs_quiesce_run(&s,&b,&io,&f)==(positive?-EPROTO:-EIO));
    assert(!s.completed&&f.reads==point&&s.quarantine==(s.writes!=0));
    assert(f.writes==s.writes&&f.writes<=3);
    assert(a733_ufs_quiesce_run(&s,&b,&io,&f)==-EALREADY&&f.reads==point);
  }
  for(unsigned point=1;point<=3;point++)for(unsigned positive=0;positive<2;positive++){
    init(&f,&b,&s);f.fail_write=point;f.fault=positive?9:-EIO;
    assert(a733_ufs_quiesce_run(&s,&b,&io,&f)==(positive?-EPROTO:-EIO));
    assert(!s.completed&&s.quarantine&&s.writes==point&&f.writes==point);
  }
  for(unsigned field=0;field<14;field++){
    init(&f,&b,&s);
    switch(field){
      case 0:b.raw[6]=1;break;
      case 1:f.regs[0]^=1;break;case 2:f.regs[2]=0x310;break;
      case 3:f.regs[0x18/4]=1;break;case 4:f.regs[0x20/4]=1;break;
      case 5:f.regs[0x4c/4]=1;break;case 6:f.regs[0x50/4]=1;break;
      case 7:f.regs[0x54/4]=1;break;case 8:f.regs[0x70/4]=1;break;
      case 9:f.regs[0x58/4]=1;break;case 10:f.regs[0x78/4]=1;break;
      case 11:f.regs[0x24/4]=0;break;case 12:f.regs[0x60/4]=0;break;
      case 13:f.regs[0x30/4]=0;break;
    }
    assert(a733_ufs_quiesce_run(&s,&b,&io,&f)<0&&!f.writes&&!s.quarantine);
    if(field==0)assert(!f.reads);
  }
  init(&f,&b,&s);f.regs[0x24/4]=f.regs[0x60/4]=f.regs[0x80/4]=0;
  assert(!a733_ufs_quiesce_run(&s,&b,&io,&f)&&s.completed&&!s.writes&&f.reads==32);
  init(&f,&b,&s);f.lag=7;
  assert(!a733_ufs_quiesce_run(&s,&b,&io,&f)&&s.completed&&s.polls==9);
  for(unsigned frozen=0;frozen<2;frozen++){
    init(&f,&b,&s);f.lag=1000;f.frozen=frozen;
    assert(a733_ufs_quiesce_run(&s,&b,&io,&f)==-ETIMEDOUT);
    assert(s.quarantine&&s.writes==1&&s.polls==A733_UFS_QUIESCE_POLLS);
  }
  init(&f,&b,&s);f.jump=true;
  assert(a733_ufs_quiesce_run(&s,&b,&io,&f)==-ETIMEDOUT&&s.quarantine&&s.polls==1);
  init(&f,&b,&s);f.mutate=true;
  assert(a733_ufs_quiesce_run(&s,&b,&io,&f)==-EAGAIN&&s.quarantine&&s.writes==1);
  puts("Quiesce production core PASS: 454 read faults + 6 write faults, 14 preflight refusals, timeout/frozen timer, delayed ACK, base mutation, one-shot, report bounds; no hardware");
}
