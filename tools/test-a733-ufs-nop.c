/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "a733_ufs_nop.h"
struct fixture
{
  uint32_t regs[33], ticks;
  unsigned reads,writes,maps,cleans,invalidates,delays;
  unsigned fail_read,fail_write,fail_map,bad_map,corrupt;
  int fault;
  bool stall,frozen,late;
  uint8_t *arena;
};
static int rd(void *ctx,uint32_t off,uint32_t *value)
{
  struct fixture *f=ctx;++f->reads;
  assert(!(off&3)&&off<=0x80);
  if(f->reads==f->fail_read)return f->fault;
  *value=f->regs[off/4];return 0;
}
static int wr(void *ctx,uint32_t off,uint32_t value)
{
  struct fixture *f=ctx;
  const uint32_t offsets[]={0x50,0x54,0x60,0x58,0x60};
  uint32_t values[]={(uint32_t)(uintptr_t)f->arena+4096,0,1,1,0};
  assert(f->writes<5&&off==offsets[f->writes]&&value==values[f->writes]);
  ++f->writes;f->regs[off/4]=value;
  if(off==0x58&&!f->stall){
    uint8_t *m=f->arena+4096,*r=m+A733_UFS_RSP_OFFSET;
    assert(f->cleans==1&&!f->invalidates);
    assert(ufs_getle32(m)==0x11000000&&ufs_getle32(m+28)==0x01000000);
    assert(!m[A733_UFS_UCD_OFFSET]&&m[A733_UFS_UCD_OFFSET+3]==0);
    assert(ufs_getle32(m+8)==15);
    ufs_le32(m+8,0);r[0]=0x20;r[3]=0;
    f->regs[0x58/4]=0;f->regs[0x20/4]=1;
    switch(f->corrupt){
      case 1:f->arena[0]^=1;break;
      case 2:m[A733_UFS_DMA_SIZE]^=1;break;
      case 3:m[0]^=1;break;case 4:m[32]^=1;break;
      case 5:m[A733_UFS_UCD_OFFSET]^=1;break;
      case 6:r[32]=1;break;case 7:m[A733_UFS_DATA_OFFSET]=1;break;
      case 8:ufs_le32(m+8,15);break;case 9:r[0]=0x21;break;
      case 10:r[3]=1;break;case 11:r[2]=1;break;case 12:r[6]=1;break;
      case 13:r[8]=1;break;case 14:r[11]=1;break;
      case 15:f->regs[0x20/4]=0x10000;break;
      case 16:f->regs[0x50/4]^=0x400;break;
      case 17:f->regs[0x58/4]=2;break;
    }
  }
  return f->writes==f->fail_write?f->fault:0;
}
static int translate(void *ctx,uintptr_t va,uint64_t *pa)
{
  struct fixture *f=ctx;++f->maps;
  assert(!(va&4095));
  if(f->maps==f->fail_map)return f->fault;
  *pa=va+(f->maps==f->bad_map?4096:0);return 0;
}
static void clean(void *ctx,uintptr_t va,size_t size)
{
  struct fixture *f=ctx;
  assert(va==(uintptr_t)f->arena&&size==A733_UFS_NOP_ARENA&&!f->writes);
  ++f->cleans;
}
static void invalidate(void *ctx,uintptr_t va,size_t size)
{
  struct fixture *f=ctx;
  assert(va==(uintptr_t)f->arena&&size==A733_UFS_NOP_ARENA&&f->writes==4);
  ++f->invalidates;
}
static uint32_t now(void *ctx)
{struct fixture *f=ctx;return f->late?f->ticks+=1001:f->ticks;}
static void delay(void *ctx)
{struct fixture *f=ctx;++f->delays;if(!f->frozen)++f->ticks;}
static const struct a733_ufs_nop_io io={rd,wr,translate,clean,invalidate,now,delay};
static void init(struct fixture *f,struct a733_ufs_board *b,
                 struct a733_ufs_quiesce *q,struct a733_ufs_nop *s,uint8_t *arena)
{
  memset(f,0,sizeof(*f));memset(b,0,sizeof(*b));memset(q,0,sizeof(*q));memset(s,0,sizeof(*s));
  f->arena=arena;b->stable=true;b->reads=26;b->gates=127;b->resets=15;b->dcxo_hz=26000000;
  b->raw[0]=0xf8675f00;b->raw[1]=0xff126310;b->raw[6]=0x30001;
  b->raw[7]=0x81000000;b->raw[8]=0x80000018;b->raw[9]=0xf0001;
  b->raw[11]=0x183fb0f7;b->raw[12]=0x511;
  f->regs[0]=0x11870f1f;f->regs[2]=0x300;f->regs[0x30/4]=0x10f;
  f->regs[0x34/4]=1;f->regs[0x50/4]=0xbfeb4c00;
  q->completed=true;q->after.stable=true;q->after.raw[8]=0xbfeb4c00;
}
int main(void)
{
  uint8_t *arena=mmap((void *)0x44000000,A733_UFS_NOP_ARENA,PROT_READ|PROT_WRITE,
                      MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
  assert(arena==(void *)0x44000000);
  struct fixture f;struct a733_ufs_board b;struct a733_ufs_quiesce q;struct a733_ufs_nop s;
  init(&f,&b,&q,&s,arena);
  assert(!a733_ufs_nop_run(&s,&b,&q,arena,&io,&f));
  assert(s.completed&&s.stopped&&s.guards&&!s.quarantine&&s.writes==5);
  assert(s.pages==19&&f.maps==19&&f.cleans==1&&f.invalidates==1);
  assert(f.writes==5&&s.submitted&&s.published&&!a733_ufs_board_hci_allowed());
  unsigned reads=f.reads;
  assert(a733_ufs_nop_run(&s,&b,&q,arena,&io,&f)==-EALREADY&&f.reads==reads);
  unsigned faults=0;
  for(unsigned p=1;p<=reads;p++)for(unsigned positive=0;positive<2;positive++){
    init(&f,&b,&q,&s,arena);f.fail_read=p;f.fault=positive?3:-EIO;
    assert(a733_ufs_nop_run(&s,&b,&q,arena,&io,&f)==(positive?-EPROTO:-EIO));
    assert(!s.completed&&f.reads==p&&s.quarantine==(s.writes!=0));++faults;
  }
  for(unsigned p=1;p<=5;p++)for(unsigned positive=0;positive<2;positive++){
    init(&f,&b,&q,&s,arena);f.fail_write=p;f.fault=positive?2:-EIO;
    assert(a733_ufs_nop_run(&s,&b,&q,arena,&io,&f)==(positive?-EPROTO:-EIO));
    assert(s.quarantine&&!s.completed&&s.writes==p);++faults;
  }
  for(unsigned p=1;p<=19;p++)for(unsigned kind=0;kind<3;kind++){
    init(&f,&b,&q,&s,arena);
    if(kind==2)f.bad_map=p;else{f.fail_map=p;f.fault=kind?4:-EFAULT;}
    assert(a733_ufs_nop_run(&s,&b,&q,arena,&io,&f)==(kind==2?-EADDRNOTAVAIL:kind?-EPROTO:-EFAULT));
    assert(!s.writes&&!s.quarantine&&!f.cleans&&f.maps==p);++faults;
  }
  for(unsigned c=1;c<=17;c++){
    init(&f,&b,&q,&s,arena);f.corrupt=c;
    assert(a733_ufs_nop_run(&s,&b,&q,arena,&io,&f)<0&&s.quarantine&&!s.completed&&s.writes==4);
  }
  for(unsigned frozen=0;frozen<2;frozen++){
    init(&f,&b,&q,&s,arena);f.stall=true;f.frozen=frozen;
    assert(a733_ufs_nop_run(&s,&b,&q,arena,&io,&f)==-ETIMEDOUT&&s.quarantine);
    assert(f.delays<=10000&&s.writes==4&&!f.invalidates);
  }
  init(&f,&b,&q,&s,arena);f.late=true;
  assert(a733_ufs_nop_run(&s,&b,&q,arena,&io,&f)==-ETIMEDOUT&&s.writes==3);
  for(unsigned c=0;c<8;c++){
    init(&f,&b,&q,&s,arena);
    switch(c){case 0:q.completed=false;break;case 1:q.quarantine=true;break;
      case 2:q.after.pending=true;break;case 3:b.raw[6]=1;break;
      case 4:f.regs[0x58/4]=1;break;case 5:f.regs[0x24/4]=1;break;
      case 6:f.regs[0x60/4]=1;break;case 7:f.regs[0x50/4]^=0x400;break;}
    assert(a733_ufs_nop_run(&s,&b,&q,arena,&io,&f)<0&&!s.writes&&!s.quarantine);
  }
  init(&f,&b,&q,&s,arena);
  assert(a733_ufs_nop_run(&s,&b,&q,arena+1,&io,&f)==-EADDRNOTAVAIL&&!f.reads);
  init(&f,&b,&q,&s,arena);
  assert(a733_ufs_nop_run(&s,&b,&q,(void *)0x100000000ULL,&io,&f)==-EADDRNOTAVAIL&&!f.reads);
  init(&f,&b,&q,&s,arena);assert(!a733_ufs_nop_run(&s,&b,&q,arena,&io,&f));
  char text[2048];
  for(unsigned cap=1;cap<sizeof(text);cap++){
    memset(text,0xa5,sizeof(text));size_t n=a733_ufs_nop_report(&s,text,cap);
    assert(n<cap&&text[n]==0&&(unsigned char)text[cap]==0xa5);
  }
  assert(!munmap(arena,A733_UFS_NOP_ARENA)); /* Host teardown only. */
  printf("NOP production core PASS: %u successful reads, %u injected read/write/map faults; 17 corruptions, deadlines/frozen timer, preflight/one-shot/report guards; NOT hardware\n",reads,faults);
}
