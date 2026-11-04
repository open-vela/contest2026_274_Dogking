/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "a733_ufs_handoff.h"
static const uint32_t offsets[]={0,8,0x18,0x20,0x24,0x30,0x34,0x4c,
  0x50,0x54,0x58,0x60,0x70,0x74,0x78,0x80};
struct fixture {unsigned calls,fail,change;int error;uint32_t values[16];};
static int read_fixture(void *context,uint32_t offset,uint32_t *value)
{
  struct fixture *f=context;unsigned i=f->calls%16;
  assert(offset==offsets[i]);++f->calls;
  if(f->calls==f->fail)return f->error;
  *value=f->values[i];if(f->calls==f->change)*value^=1;
  return 0;
}
static void init(struct fixture *f,struct a733_ufs_board *b)
{
  memset(f,0,sizeof(*f));memset(b,0,sizeof(*b));
  b->stable=true;b->reads=26;b->gates=127;b->resets=15;b->dcxo_hz=26000000;
  b->raw[0]=0xf8675f00;b->raw[1]=0xff126310;b->raw[6]=0x30001;b->raw[7]=0x81000000;
  b->raw[8]=0x80000018;b->raw[9]=0xf0001;b->raw[11]=0x183fb0f7;b->raw[12]=0x511;
  f->values[0]=0x100001f;f->values[1]=0x300;f->values[5]=0xf;f->values[6]=1;
}
int main(void)
{
  struct a733_ufs_handoff s;struct a733_ufs_board b;struct fixture f;char text[2048];
  init(&f,&b);assert(!a733_ufs_handoff_profile(NULL));
  assert(a733_ufs_handoff_snapshot(NULL,&b,read_fixture,&f)==-EINVAL);
  assert(a733_ufs_handoff_snapshot(&s,&b,NULL,&f)==-EINVAL);
  assert(!a733_ufs_handoff_snapshot(&s,&b,read_fixture,&f));
  assert(f.calls==32&&s.stable&&s.link_ready&&!s.pending&&!s.needs_quiesce);
  assert(!a733_ufs_board_hci_allowed());
  for(unsigned i=0;i<14;i++){
    init(&f,&b);
    switch(i){
      case 0:b.stable=false;break;case 1:b.error=-EIO;break;
      case 2:b.reads=0;break;case 3:b.gates^=1;break;case 4:b.resets^=1;break;
      case 5:b.dcxo_hz=24000000;break;case 6:b.raw[0]^=1;break;
      case 7:b.raw[1]^=1;break;case 8:b.raw[7]^=1;break;
      case 9:b.raw[8]^=1;break;case 10:b.raw[9]^=1;break;
      case 11:b.raw[11]^=0x80000000;break;case 12:b.raw[12]^=1;break;
      case 13:b.raw[6]^=0x30000;break;
    }
    assert(a733_ufs_handoff_snapshot(&s,&b,read_fixture,&f)==-EXDEV&&!f.calls);
  }
  for(unsigned point=1;point<=32;point++)for(unsigned e=0;e<2;e++){
    init(&f,&b);f.fail=point;f.error=e?1:-EIO;
    assert(a733_ufs_handoff_snapshot(&s,&b,read_fixture,&f)==(e?-EPROTO:-EIO));
    assert(f.calls==point&&!s.stable&&s.last_offset==offsets[(point-1)%16]);
  }
  for(unsigned point=17;point<=32;point++){
    init(&f,&b);f.change=point;
    assert(a733_ufs_handoff_snapshot(&s,&b,read_fixture,&f)==-EAGAIN);
    assert(f.calls==point&&!s.stable);
  }
  for(unsigned i=0;i<5;i++){
    init(&f,&b);f.values[i==0?0:i<3?1:6]=i==0?UINT32_MAX:i==1?0x100:i==2?0x400:i==3?0:3;
    assert(a733_ufs_handoff_snapshot(&s,&b,read_fixture,&f)==-ENOLINK);
  }
  init(&f,&b);f.values[3]=0x10000;
  assert(a733_ufs_handoff_snapshot(&s,&b,read_fixture,&f)==-EIO);
  for(unsigned i=0;i<6;i++){
    static const unsigned fields[]={2,4,10,11,14,15};
    init(&f,&b);f.values[fields[i]]=1;
    assert(!a733_ufs_handoff_snapshot(&s,&b,read_fixture,&f));
    assert(s.needs_quiesce&&s.pending==(i==2||i==4));
    assert(!a733_ufs_board_hci_allowed());
  }
  assert(a733_ufs_handoff_report(&s,text,sizeof(text))==strlen(text));
  assert(strstr(text,"media-commands=0")&&strstr(text,"attach-blocked=1"));
  assert(!a733_ufs_handoff_report(&s,text,1)&&text[0]==0);
  assert(a733_ufs_handoff_report(&s,text,8)==7&&text[7]==0);
  assert(!a733_ufs_handoff_report(NULL,text,sizeof(text)));
  assert(!a733_ufs_handoff_report(&s,NULL,sizeof(text)));
  puts("Handoff production core: 14 pre-MMIO refusals, 80 read/change faults, invalid states, queue observation, report bounds PASS; no hardware");
}
