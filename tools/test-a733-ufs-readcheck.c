/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "a733_ufs_readcheck.h"
struct fixture
{
  uint32_t regs[33],ticks,bs;uint64_t blocks;
  unsigned reads,writes,maps,commands,cleans,invalidates,delays;
  unsigned fail_read,fail_write,fail_map,bad_map,corrupt,corrupt_at,ua,gpt;
  int fault;bool stall,frozen,late;
  uint8_t *arena;uint8_t head[32768];
};
static uint32_t crc(const uint8_t *data,size_t n)
{
  uint32_t c=~0u;
  for(size_t i=0;i<n;i++)
    {c^=data[i];for(unsigned j=0;j<8;j++)c=(c>>1)^((0-(c&1))&0xedb88320u);}
  return ~c;
}
static void le64(uint8_t *p,uint64_t value)
{ufs_le32(p,(uint32_t)value);ufs_le32(p+4,(uint32_t)(value>>32));}
static void media(struct fixture *f,uint64_t lba,uint8_t *d,unsigned bytes)
{
  for(unsigned i=0;i<bytes;i++)
    {
      uint64_t pos=lba*f->bs+i;
      d[i]=pos<sizeof(f->head)?f->head[pos]:(uint8_t)(pos^(pos>>13)^(pos>>23));
    }
}
static int rd(void *ctx,uint32_t off,uint32_t *value)
{
  struct fixture *f=ctx;++f->reads;assert(!(off&3)&&off<=0x80);
  if(f->reads==f->fail_read)return f->fault;
  *value=f->regs[off/4];return 0;
}
static int wr(void *ctx,uint32_t off,uint32_t value)
{
  struct fixture *f=ctx;
  if(f->writes<2)
    {assert(off==(f->writes?0x54:0x50));assert(value==(f->writes?0:(uint32_t)(uintptr_t)f->arena+4096));}
  else
    {
      const uint32_t offsets[]={0x20,0x60,0x58,0x60},values[]={1,1,1,0};
      unsigned phase=(f->writes-2)%4;assert(off==offsets[phase]&&value==values[phase]);
    }
  ++f->writes;
  if(off==0x20)f->regs[off/4]&=~value;
  else f->regs[off/4]=value;
  if(off==0x58&&!f->stall)
    {
      uint8_t *m=f->arena+4096,*cmd=m+A733_UFS_UCD_OFFSET,*r=m+A733_UFS_RSP_OFFSET;
      uint8_t *d=m+A733_UFS_DATA_OFFSET;
      unsigned op=cmd[16],bytes=ufs_getbe32(cmd+12);++f->commands;
      assert(f->regs[0x60/4]==1&&f->cleans==f->invalidates+2);
      assert(op==0x12||op==0x9e||op==0x88);
      assert(bytes>0&&bytes<=65536&&ufs_getle32(m)==0x15000000);
      assert(cmd[0]==1&&cmd[1]==0x40&&cmd[3]==0&&ufs_getle32(m+8)==15);
      assert(ufs_getle32(m+A733_UFS_PRD_OFFSET)==(uint32_t)(uintptr_t)d);
      assert(ufs_getle32(m+A733_UFS_PRD_OFFSET+12)==bytes-1);
      assert(ufs_getle32(m+28)==0x01000001);
      r[0]=0x21;r[2]=cmd[2];ufs_le32(m+8,0);
      bool ua=(f->ua==1||f->ua==3)?f->commands==1:
               f->ua==2?f->commands<=2:(f->ua==4&&f->commands==3)||(f->ua==5&&f->commands==10);
      if(ua)
        {
          r[7]=2;ufs_be32(r+12,bytes);
          if(f->ua==3){r[11]=10;r[33]=8;r[34]=0x72;r[35]=6;r[36]=0x29;}
          else{r[11]=20;r[33]=18;r[34]=0x70;r[36]=6;r[41]=10;r[46]=0x29;}
        }
      else if(op==0x12)
        {d[0]=0;d[4]=31;memset(d+8,' ',28);memcpy(d+8,"KINGSTON HOST FIXTURE",21);}
      else if(op==0x9e)
        {ufs_be32(d,(uint32_t)((f->blocks-1)>>32));ufs_be32(d+4,(uint32_t)(f->blocks-1));ufs_be32(d+8,f->bs);}
      else
        {
          uint64_t lba=ufs_getbe64(cmd+18);unsigned count=ufs_getbe32(cmd+26);
          assert(count&&lba<=f->blocks-count&&bytes==count*f->bs);media(f,lba,d,bytes);
          if(f->corrupt==16&&f->commands==4)d[100]^=1;
        }
      f->regs[0x58/4]=0;f->regs[0x20/4]=1;
      if(f->commands==(f->corrupt_at?f->corrupt_at:1))switch(f->corrupt)
        {
          case 1:f->arena[0]^=1;break;case 2:m[A733_UFS_DMA_SIZE]^=1;break;
          case 3:m[32]=1;break;case 4:m[A733_UFS_UCD_OFFSET]^=1;break;
          case 5:m[A733_UFS_PRD_OFFSET]^=1;break;case 6:d[bytes]=1;break;
          case 7:ufs_le32(m+8,7);break;case 8:r[0]=0x20;break;
          case 9:r[3]=1;break;case 10:r[2]^=1;break;case 11:r[8]=1;break;
          case 12:r[11]=1;break;case 13:r[9]=2;break;
          case 14:f->regs[0x20/4]=0x10000;break;
          case 15:f->regs[0x58/4]=2;break;
          case 17:r[1]=0x20;ufs_be32(r+12,1);break;
          case 18:r[7]=2;r[33]=255;break;
          case 19:ufs_le32(m+8,0x100);break;
          case 20:r[9]=1;break; /* Valid device exception, reported only. */
          case 21:d[0]=1;break;
        }
    }
  return f->writes==f->fail_write?f->fault:0;
}
static int translate(void *ctx,uintptr_t va,uint64_t *pa)
{
  struct fixture *f=ctx;++f->maps;assert(!(va&4095));
  if(f->maps==f->fail_map)return f->fault;
  *pa=va+(f->maps==f->bad_map?4096:0);return 0;
}
static void clean(void *ctx,uintptr_t va,size_t bytes)
{struct fixture *f=ctx;assert(va==(uintptr_t)f->arena&&bytes==A733_UFS_NOP_ARENA);++f->cleans;}
static void invalidate(void *ctx,uintptr_t va,size_t bytes)
{struct fixture *f=ctx;assert(va==(uintptr_t)f->arena&&bytes==A733_UFS_NOP_ARENA);++f->invalidates;}
static uint32_t now(void *ctx)
{struct fixture *f=ctx;return f->late?f->ticks+=3001:f->ticks;}
static void delay(void *ctx)
{struct fixture *f=ctx;++f->delays;if(!f->frozen)++f->ticks;}
static const struct a733_ufs_nop_io io={rd,wr,translate,clean,invalidate,now,delay};
static void init(struct fixture *f,struct a733_ufs_board *b,struct a733_ufs_nop *nop,
                 struct a733_ufs_readcheck *s,uint8_t *arena,unsigned bs,unsigned gpt)
{
  memset(f,0,sizeof(*f));memset(b,0,sizeof(*b));memset(nop,0,sizeof(*nop));memset(s,0,sizeof(*s));
  f->arena=arena;f->bs=bs;f->blocks=bs==4096?31256576:1048576;
  b->stable=true;b->reads=26;b->gates=127;b->resets=15;b->dcxo_hz=26000000;
  b->raw[0]=0xf8675f00;b->raw[1]=0xff126310;b->raw[6]=0x30001;
  b->raw[7]=0x81000000;b->raw[8]=0x80000018;b->raw[9]=0xf0001;b->raw[11]=0x183fb0f7;b->raw[12]=0x511;
  nop->completed=nop->stopped=nop->guards=true;nop->physical=0x43001000;
  f->regs[0]=0x11870f1f;f->regs[2]=0x300;f->regs[8]=1;
  f->regs[0x30/4]=0x10f;f->regs[0x34/4]=1;f->regs[0x50/4]=(uint32_t)nop->physical;
  if(!gpt)return;
  uint8_t *h=f->head+bs,*e=f->head+2*bs;
  f->head[510]=0x55;f->head[511]=0xaa;f->head[450]=0xee;
  memcpy(h,"EFI PART",8);ufs_le32(h+8,0x10000);ufs_le32(h+12,92);
  le64(h+24,1);le64(h+32,f->blocks-1);le64(h+40,2+16384/bs);le64(h+48,f->blocks-34);
  le64(h+72,2);ufs_le32(h+80,128);ufs_le32(h+84,128);
  e[0]=1;le64(e+32,128);le64(e+40,1024);
  ufs_le32(h+88,crc(e,16384));ufs_le32(h+16,crc(h,92));
  if(gpt==2)h[16]^=1;
  if(gpt==3)e[100]^=1;
  if(gpt==4){ufs_le32(h+80,256);ufs_le32(h+16,0);ufs_le32(h+16,crc(h,92));}
}
int main(void)
{
  assert(crc((const uint8_t *)"123456789",9)==0xcbf43926);
  uint8_t *arena=mmap((void *)0x44000000,A733_UFS_NOP_ARENA,PROT_READ|PROT_WRITE,
                      MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
  assert(arena==(void *)0x44000000);
  static struct fixture f;static struct a733_ufs_readcheck s;
  struct a733_ufs_board b;struct a733_ufs_nop nop;
  init(&f,&b,&nop,&s,arena,4096,1);
  assert(!a733_ufs_readcheck_run(&s,&b,&nop,arena,0,&io,&f));
  assert(s.completed&&s.stopped&&s.guards&&!s.quarantine&&s.pages==19&&s.samples==3);
  assert(s.identity&&s.commands==9&&s.writes==38&&s.blocks==f.blocks&&s.blocksize==4096);
  assert(s.gpt_header_valid&&s.gpt_entries_valid&&s.gpt_partitions==1&&!s.gpt_error);
  assert(s.readbytes==114756&&f.maps==19&&f.cleans==10&&f.invalidates==9);
  for(unsigned i=0;i<3;i++)assert(s.crc[i]==s.repeat_crc[i]);
  unsigned reads=f.reads,writes=f.writes,faults=0;
  assert(a733_ufs_readcheck_run(&s,&b,&nop,arena,0,&io,&f)==-EALREADY&&f.reads==reads);
  for(unsigned p=1;p<=reads;p++)for(unsigned positive=0;positive<2;positive++)
    {
      init(&f,&b,&nop,&s,arena,4096,1);f.fail_read=p;f.fault=positive?5:-EIO;
      assert(a733_ufs_readcheck_run(&s,&b,&nop,arena,0,&io,&f)==(positive?-EPROTO:-EIO));
      assert(!s.completed&&f.reads==p&&s.quarantine==(s.writes!=0));++faults;
    }
  for(unsigned p=1;p<=writes;p++)for(unsigned positive=0;positive<2;positive++)
    {
      init(&f,&b,&nop,&s,arena,4096,1);f.fail_write=p;f.fault=positive?1:-EIO;
      assert(a733_ufs_readcheck_run(&s,&b,&nop,arena,0,&io,&f)==(positive?-EPROTO:-EIO));
      assert(!s.completed&&s.writes==p&&s.quarantine);++faults;
    }
  for(unsigned p=1;p<=19;p++)for(unsigned kind=0;kind<3;kind++)
    {
      init(&f,&b,&nop,&s,arena,4096,1);
      if(kind==2)f.bad_map=p;else{f.fail_map=p;f.fault=kind?2:-EFAULT;}
      assert(a733_ufs_readcheck_run(&s,&b,&nop,arena,0,&io,&f)==(kind==2?-EADDRNOTAVAIL:kind?-EPROTO:-EFAULT));
      assert(!s.writes&&!s.quarantine);++faults;
    }
  for(unsigned c=1;c<=21;c++)
    {
      init(&f,&b,&nop,&s,arena,4096,1);f.corrupt=c;
      int ret=a733_ufs_readcheck_run(&s,&b,&nop,arena,0,&io,&f);
      if(c==20)assert(!ret&&s.exceptions==1&&s.completed);
      else assert(ret<0&&s.quarantine&&!s.completed);
    }
  for(unsigned ua=1;ua<=4;ua++)
    {
      init(&f,&b,&nop,&s,arena,4096,1);f.ua=ua;
      int ret=a733_ufs_readcheck_run(&s,&b,&nop,arena,0,&io,&f);
      if(ua==1||ua==3)assert(!ret&&s.ua_retries==1&&s.commands==10&&s.completed);
      else assert(ret==-EREMOTEIO&&!s.completed&&s.quarantine&&s.commands==(ua==2?2:3));
    }
  for(unsigned bs=512;bs<=4096;bs*=8)for(unsigned gpt=0;gpt<=4;gpt++)
    {
      init(&f,&b,&nop,&s,arena,bs,gpt);
      assert(!a733_ufs_readcheck_run(&s,&b,&nop,arena,3,&io,&f));
      assert(s.samples==3&&s.identity&&s.lun==3&&s.blocksize==bs);
      assert(s.gpt_entries_valid==(gpt==1));
      if(gpt==2||gpt==3)assert(s.gpt_error==-EBADMSG);
      if(gpt==4)assert(s.gpt_error==-E2BIG);
    }
  for(unsigned c=0;c<10;c++)
    {
      init(&f,&b,&nop,&s,arena,4096,1);
      switch(c){case 0:nop.completed=false;break;case 1:nop.quarantine=true;break;
        case 2:nop.stopped=false;break;case 3:nop.guards=false;break;
        case 4:f.regs[8]=0;break;case 5:f.regs[0x24/4]=1;break;
        case 6:f.regs[0x50/4]^=0x400;break;case 7:f.regs[0x60/4]=1;break;
        case 8:b.raw[6]=0;break;case 9:nop.physical=0x44001000;break;}
      assert(a733_ufs_readcheck_run(&s,&b,&nop,arena,0,&io,&f)<0&&!s.writes&&!s.quarantine);
    }
  for(unsigned frozen=0;frozen<2;frozen++)
    {
      init(&f,&b,&nop,&s,arena,4096,1);f.stall=true;f.frozen=frozen;
      assert(a733_ufs_readcheck_run(&s,&b,&nop,arena,0,&io,&f)==-ETIMEDOUT&&s.quarantine&&s.commands==1);
      assert(f.delays<=30000&&!f.invalidates);
    }
  init(&f,&b,&nop,&s,arena,4096,1);f.late=true;
  assert(a733_ufs_readcheck_run(&s,&b,&nop,arena,0,&io,&f)==-ETIMEDOUT&&s.quarantine);
  init(&f,&b,&nop,&s,arena,4096,1);
  assert(a733_ufs_readcheck_run(&s,&b,&nop,arena+1,0,&io,&f)==-EADDRNOTAVAIL&&!s.writes);
  init(&f,&b,&nop,&s,arena,4096,1);
  assert(a733_ufs_readcheck_run(&s,&b,&nop,arena,8,&io,&f)==-EINVAL&&!s.attempted&&!s.writes);
  init(&f,&b,&nop,&s,arena,4096,1);assert(!a733_ufs_readcheck_run(&s,&b,&nop,arena,0,&io,&f));
  char text[2048];
  for(size_t cap=1;cap<sizeof(text);cap++)
    {
      memset(text,0xa5,sizeof(text));size_t n=a733_ufs_readcheck_report(&s,text,cap);
      assert(n<cap&&!text[n]&&(unsigned char)text[cap]==0xa5);
    }
  /* Repeatable production reader after qualification; real caller buffers
   * remain CPU-only. No fixture-only permit or production state reset. */
  static uint8_t buffer[1048576],expected[1048576];
  init(&f,&b,&nop,&s,arena,4096,1);assert(!a733_ufs_readcheck_run(&s,&b,&nop,arena,0,&io,&f));
  assert(!a733_ufs_reader_ready(&s,&b,arena,&io,&f));
  unsigned initial_reads=f.reads,initial_writes=f.writes;
  assert(!a733_ufs_reader_read(&s,&b,arena,buffer,8,16,&io,&f));
  media(&f,8,expected,65536);assert(!memcmp(buffer,expected,65536));
  assert(s.reader_calls==1&&s.reader_bytes==65536&&s.commands==10&&s.stopped&&s.guards);
  unsigned reader_reads=f.reads-initial_reads,reader_writes=f.writes-initial_writes;
  assert(reader_writes==4);
  assert(!a733_ufs_reader_read(&s,&b,arena,buffer,31,256,&io,&f));
  media(&f,31,expected,sizeof(expected));assert(!memcmp(buffer,expected,sizeof(buffer)));
  assert(s.commands==26&&s.reader_bytes==1114112&&!s.reader_active&&!s.reader_budget);
  unsigned previous_reads=f.reads,previous_writes=f.writes;
  assert(a733_ufs_reader_read(&s,&b,arena,buffer,0,257,&io,&f)==-E2BIG);
  assert(a733_ufs_reader_read(&s,&b,arena,buffer,s.blocks,1,&io,&f)==-EINVAL);
  assert(a733_ufs_reader_read(&s,&b,arena,arena,0,1,&io,&f)==-EINVAL);
  assert(a733_ufs_reader_read(&s,&b,arena,(uint8_t *)&s,0,1,&io,&f)==-EINVAL);
  assert(!a733_ufs_reader_read(&s,&b,arena,NULL,s.blocks,0,&io,&f));
  assert(f.reads==previous_reads&&f.writes==previous_writes);
  unsigned reader_faults=0;
  for(unsigned p=1;p<=reader_reads;p++)
    {
      init(&f,&b,&nop,&s,arena,4096,1);assert(!a733_ufs_readcheck_run(&s,&b,&nop,arena,0,&io,&f));
      f.fail_read=f.reads+p;f.fault=-EIO;
      assert(a733_ufs_reader_read(&s,&b,arena,buffer,8,16,&io,&f)==-EIO&&s.quarantine);
      unsigned old=f.reads;assert(a733_ufs_reader_read(&s,&b,arena,buffer,8,16,&io,&f)==-EACCES&&f.reads==old);
      ++reader_faults;
    }
  for(unsigned p=1;p<=reader_writes;p++)
    {
      init(&f,&b,&nop,&s,arena,4096,1);assert(!a733_ufs_readcheck_run(&s,&b,&nop,arena,0,&io,&f));
      f.fail_write=f.writes+p;f.fault=1;
      assert(a733_ufs_reader_read(&s,&b,arena,buffer,8,16,&io,&f)==-EPROTO&&s.quarantine);
      ++reader_faults;
    }
  for(unsigned p=1;p<=19;p++)
    {
      init(&f,&b,&nop,&s,arena,4096,1);assert(!a733_ufs_readcheck_run(&s,&b,&nop,arena,0,&io,&f));
      f.bad_map=f.maps+p;
      assert(a733_ufs_reader_read(&s,&b,arena,buffer,8,16,&io,&f)==-EFAULT&&s.quarantine);
      ++reader_faults;
    }
  for(unsigned c=1;c<=21;c++)if(c!=16&&c!=21)
    {
      init(&f,&b,&nop,&s,arena,4096,1);assert(!a733_ufs_readcheck_run(&s,&b,&nop,arena,0,&io,&f));
      f.corrupt=c;f.corrupt_at=10;
      /* Full-size transfer has no trailing data byte inside DMA payload. */
      unsigned sectors=c==6?1:16;
      int ret=a733_ufs_reader_read(&s,&b,arena,buffer,8,sectors,&io,&f);
      if(c==20)assert(!ret&&s.exceptions==1);else assert(ret<0&&s.quarantine);
    }
  for(unsigned c=0;c<6;c++)
    {
      init(&f,&b,&nop,&s,arena,4096,1);assert(!a733_ufs_readcheck_run(&s,&b,&nop,arena,0,&io,&f));
      switch(c){case 0:f.ua=5;break;case 1:f.stall=true;break;
        case 2:arena[0]^=1;break;case 3:f.regs[0x50/4]^=0x400;break;
        case 4:s.reader_active=true;break;case 5:s.commands=UINT32_MAX-3;break;}
      assert(a733_ufs_reader_read(&s,&b,arena,buffer,8,16,&io,&f)<0);
      if(c<4)assert(s.quarantine);
    }
  for(unsigned bs=512;bs<=4096;bs*=8)
    {
      init(&f,&b,&nop,&s,arena,bs,1);f.blocks=UINT64_C(0x100004000);
      /* Regenerate no GPT: fixtures intentionally exercise high 64-bit LBA. */
      memset(f.head,0,sizeof(f.head));assert(!a733_ufs_readcheck_run(&s,&b,&nop,arena,0,&io,&f));
      assert(!a733_ufs_reader_read(&s,&b,arena,buffer,UINT64_C(0x100000123),65536/bs,&io,&f));
      media(&f,UINT64_C(0x100000123),expected,65536);assert(!memcmp(buffer,expected,65536));
    }
  assert(!munmap(arena,A733_UFS_NOP_ARENA)); /* HOST teardown ONLY. */
  printf("Reader v171 PASS: 64KiB/1MiB exact bytes, high64 LBA, %u reader faults, 19 mutations, UA refusal, timeout, bounds/quarantine; NOT hardware\n",reader_faults);
  printf("Readcheck production core PASS: %u reads/%u writes, %u fault injections; 21 mutations, 512/4096 geometry, GPT variants, UA bounds, timeouts, one-shot/report; NOT hardware\n",reads,writes,faults);
}
