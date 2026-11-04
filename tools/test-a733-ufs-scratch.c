/* SPDX-License-Identifier: Apache-2.0 */
/* Reuse the adversarial READ-only fixture, not a second production transport. */
#define main readcheck_regression_main
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#include "test-a733-ufs-readcheck.c"
#pragma GCC diagnostic pop
#undef main
#include "a733_ufs_scratch.h"
#include "a733_ufs_write_wire.h"
static uint8_t medium[73728],saved[73728],work[65536];
static unsigned write_commands,flush_commands,fail_command,damage;
static bool stuck;
static int swr(void *ctx,uint32_t off,uint32_t value)
{
  struct fixture *f=ctx;uint8_t *m=f->arena+4096,*cmd=m+1024,*rsp=m+1536,*d=m+4096;
  unsigned op=cmd[16];
  if(off!=0x58||(op!=0x8a&&op!=0x35))
    {
      int ret=wr(ctx,off,value);
      if(off==0x58&&op==0x12)memcpy(d+8,"KINGSTONCY14-128        0002",28);
      if(off==0x58&&op==0x88)
        {
          uint64_t lba=ufs_getbe64(cmd+18);uint32_t bytes=ufs_getbe32(cmd+12);
          if(lba>=A733_UFS_SCRATCH_LBA-1&&lba<=A733_UFS_SCRATCH_LBA+16)
            {
              size_t pos=(size_t)(lba-(A733_UFS_SCRATCH_LBA-1))*4096;
              assert(pos+bytes<=sizeof(medium));memcpy(d,medium+pos,bytes);
            }
        }
      return ret;
    }
  assert(value==1&&f->regs[0x60/4]==1);
  assert((f->writes-2)%4==2);++f->writes;++f->commands;
  assert(cmd[0]==1&&cmd[2]==0&&cmd[3]==0&&cmd[17]==0);
  assert(ufs_getle32(m+8)==15&&ufs_getle32(m+24)==0x00800080);
  if(op==0x8a)
    {
      assert(ufs_getle32(m)==0x13000000&&cmd[1]==0x20);
      assert(ufs_getbe32(cmd+12)==65536&&ufs_getbe64(cmd+18)==31256496);
      assert(ufs_getbe32(cmd+26)==16&&ufs_getle32(m+28)==0x01000001);
      assert(ufs_getle32(m+2048)==(uint32_t)(uintptr_t)d&&ufs_getle32(m+2060)==65535);
      memcpy(medium+4096,d,65536);++write_commands;
    }
  else
    {
      assert(ufs_getle32(m)==0x11000000&&cmd[1]==0&&ufs_getbe32(cmd+12)==0);
      assert(ufs_getle32(m+28)==0x01000000);
      for(unsigned i=17;i<32;i++)assert(cmd[i]==0);
      for(unsigned i=2048;i<A733_UFS_DMA_SIZE;i++)assert(m[i]==0);
      ++flush_commands;
    }
  f->regs[0x58/4]=stuck?1:0;f->regs[0x20/4]=stuck?0:1;
  rsp[0]=0x21;ufs_le32(m+8,0);
  unsigned n=write_commands+flush_commands;
  if(n==fail_command)
    {
      switch(damage)
        {
          case 1:ufs_le32(m+8,0x100);break;
          case 2:rsp[0]^=1;break;case 3:rsp[1]=0x20;break;
          case 4:rsp[2]=1;break;case 5:rsp[3]=1;break;
          case 6:rsp[6]=1;break;case 7:rsp[9]=1;break;
          case 8:rsp[11]=1;break;case 9:rsp[33]=255;break;
          case 10:rsp[15]=1;break;case 11:m[32]=1;break;
          case 12:cmd[0]^=1;break;case 13:m[2048]^=1;break;
          case 14:d[200]^=1;break;case 15:f->arena[0]^=1;break;
          case 16:m[A733_UFS_DMA_SIZE]^=1;break;
          case 17:medium[0]^=1;break;
          case 18:medium[4096+123]^=1;break;
          case 19:rsp[7]=2;rsp[11]=20;rsp[33]=18;rsp[34]=0x70;rsp[36]=6;rsp[41]=10;rsp[46]=0x29;break;
          case 20:f->regs[0x20/4]=0x10001;break;
        }
    }
  return f->writes==f->fail_write?-EIO:0;
}
static const struct a733_ufs_nop_io sio={rd,swr,translate,clean,invalidate,now,delay};
static struct fixture f;
static struct a733_ufs_readcheck r;
static struct a733_ufs_board board;
static struct a733_ufs_scratch s;
static void setup(uint8_t *arena)
{
  struct a733_ufs_nop nop;init(&f,&board,&nop,&r,arena,4096,1);
  uint8_t *e=f.head+8192,*h=f.head+4096;
  memset(e,0,16384);
  const uint64_t ranges[3][2]={{32768,65535},{65536,679935},{679936,31256542}};
  for(unsigned i=0;i<3;i++){e[i*128]=1;le64(e+i*128+32,ranges[i][0]);le64(e+i*128+40,ranges[i][1]);}
  ufs_le32(h+88,crc(e,16384));ufs_le32(h+16,0);ufs_le32(h+16,crc(h,92));
  write_commands=flush_commands=fail_command=damage=0;stuck=false;
  for(size_t i=0;i<sizeof(medium);i++)medium[i]=(uint8_t)(i^(i>>7)^0x97);
  memcpy(saved,medium,sizeof(saved));memset(&s,0,sizeof(s));
  assert(!a733_ufs_readcheck_run(&r,&board,&nop,arena,0,&sio,&f));
  assert(!memcmp(r.model,"KINGSTONCY14-128        0002",28));
}
static int prepare(uint8_t *a)
{return a733_ufs_scratch_prepare(&s,&r,&board,a,work,&sio,&f);}
static int test(uint8_t *a)
{return a733_ufs_scratch_test(&s,&r,&board,a,work,&sio,&f);}
#ifndef A733_UFS_SCRATCH_FIXTURE_ONLY
int main(void)
{
  uint8_t *a=mmap((void *)0x44000000,A733_UFS_NOP_ARENA,PROT_READ|PROT_WRITE,
    MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);assert(a==(void *)0x44000000);
  setup(a);assert(!prepare(a)&&s.prepared&&write_commands==0&&flush_commands==0);
  assert(!memcmp(s.original,saved+4096,65536));assert(prepare(a)==-EALREADY);
  assert(!test(a)&&s.completed&&s.restored&&!s.quarantine&&s.stopped&&s.guards);
  assert(s.writes==3&&s.flushes==4&&s.patterns==2);
  assert(!memcmp(medium,saved,sizeof(saved))&&test(a)==-EALREADY);
  setup(a);assert(!prepare(a));
  unsigned begin_reads=f.reads,begin_writes=f.writes,begin_maps=f.maps;
  assert(!test(a));
  unsigned end_reads=f.reads,end_writes=f.writes,end_maps=f.maps;
  unsigned faults=0;
  for(unsigned kind=0;kind<3;kind++)
    {
      unsigned first=kind==0?begin_reads:kind==1?begin_writes:begin_maps;
      unsigned end=kind==0?end_reads:kind==1?end_writes:end_maps;
      unsigned step=kind==0?17:kind==1?1:19;
      for(unsigned p=first+1;p<=end;p+=step)
        {
          setup(a);assert(!prepare(a));f.fault=-EIO;
          if(kind==0)f.fail_read=p;else if(kind==1)f.fail_write=p;else f.fail_map=p;
          assert(test(a)<0&&!s.completed&&r.quarantine);
          unsigned commands=write_commands+flush_commands,mmio=f.writes;
          assert(test(a)<0&&write_commands+flush_commands==commands&&f.writes==mmio);
          assert(!memcmp(s.original,saved+4096,65536));++faults;
        }
    }
  for(unsigned c=1;c<=7;c++)for(unsigned d=1;d<=20;d++)
    {
      /* Neighbor corruption at the last flush is detected after restoration;
       * target corruption at each write/flush is detected before continuation. */
      setup(a);assert(!prepare(a));fail_command=c;damage=d;
      assert(test(a)<0&&!s.completed&&s.quarantine&&r.quarantine);
      unsigned commands=write_commands+flush_commands;
      unsigned expected=c+((c==2||c==4||c==6)&&(d==17||d==18)?1:0);
      assert(commands==expected&&test(a)<0&&write_commands+flush_commands==commands);
      assert(!memcmp(s.original,saved+4096,65536));++faults;
    }
  for(unsigned kind=0;kind<8;kind++)
    {
      setup(a);unsigned reads=f.reads,writes=f.writes;
      switch(kind)
        {
          case 0:r.model[0]^=1;break;case 1:r.blocks--;break;
          case 2:r.lun=1;break;case 3:r.partitions[2][1]--;break;
          case 4:r.entry_crc^=1;break;case 5:r.crc[1]^=1;break;
          case 6:r.gpt_error=-EIO;break;case 7:r.error=-EIO;break;
        }
      assert(prepare(a)<0&&!s.prepared&&!write_commands&&!flush_commands);
      if(kind!=4&&kind!=5)assert(f.reads==reads&&f.writes==writes);
      ++faults;
    }
  setup(a);assert(!prepare(a));medium[4096]^=1;
  assert(test(a)<0&&!write_commands);++faults;
  setup(a);assert(!prepare(a));s.original[0]^=1;
  assert(test(a)<0&&!write_commands);++faults;
  setup(a);assert(!prepare(a));stuck=true;f.frozen=true;
  assert(test(a)==-ETIMEDOUT&&s.quarantine&&!write_commands&&flush_commands==1);++faults;
  setup(a);assert(test(a)==-EACCES&&!write_commands);++faults;
  char report[2048];assert(a733_ufs_scratch_report(&s,report,sizeof(report))>0);
  assert(strstr(report,"lost-on-reboot=1"));
  assert(!munmap(a,A733_UFS_NOP_ARENA));
  printf("scratch production transport: restore + pinned ABI + %u faults PASS; no hardware\n",faults);
  return 0;
}
#endif
