/* SPDX-License-Identifier: Apache-2.0 */
/* Reuse production scratch qualification and its host-only media fixture. */
#define A733_UFS_SCRATCH_FIXTURE_ONLY
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "test-a733-ufs-scratch.c"
#pragma GCC diagnostic pop
#include "a733_ufs_data.h"
static struct a733_ufs_data data;
static uint8_t volume[1048576],input[1048576],output[1048576];
static uint64_t base;
static unsigned data_commands,bad_command,bad_kind;
static bool active,stall_data,bad_readback;
static int dwr(void *ctx,uint32_t off,uint32_t value)
{
  uint8_t *m=f.arena+4096,*cmd=m+1024,*rsp=m+1536,*payload=m+4096;
  unsigned op=cmd[16];
  if(!active||off!=0x58||(op!=0x8a&&op!=0x35))
    {
      int ret=swr(ctx,off,value);
      if(active&&off==0x58&&op==0x88)
        {
          uint64_t lba=ufs_getbe64(cmd+18);unsigned n=ufs_getbe32(cmd+12);
          if(lba>=base&&lba<base+256)
            {
              assert((lba-base)*4096+n<=sizeof(volume));
              memcpy(payload,volume+(size_t)(lba-base)*4096,n);
              if(bad_readback)payload[0]^=1;
            }
        }
      return ret;
    }
  assert(value==1&&f.regs[0x60/4]==1&&(f.writes-2)%4==2);
  ++f.writes;++f.commands;++data_commands;
  assert(cmd[0]==1&&cmd[2]==0&&cmd[3]==0&&cmd[17]==0);
  assert(ufs_getle32(m+8)==15&&ufs_getle32(m+24)==0x00800080);
  if(op==0x8a)
    {
      uint64_t lba=ufs_getbe64(cmd+18);uint32_t n=ufs_getbe32(cmd+26);
      assert(n&&n<=16&&lba>=A733_UFS_DATA_FIRST&&
             lba+n<=A733_UFS_DATA_FIRST+A733_UFS_DATA_BLOCKS);
      assert(lba>=base&&lba+n<=base+256);
      assert(ufs_getle32(m)==0x13000000&&cmd[1]==0x20);
      assert(ufs_getbe32(cmd+12)==n*4096&&ufs_getle32(m+28)==0x01000001);
      assert(ufs_getle32(m+2048)==(uint32_t)(uintptr_t)payload);
      assert(ufs_getle32(m+2060)==n*4096-1);
      memcpy(volume+(size_t)(lba-base)*4096,payload,n*4096);
    }
  else
    {
      assert(ufs_getle32(m)==0x11000000&&cmd[1]==0&&ufs_getbe32(cmd+12)==0);
      assert(ufs_getle32(m+28)==0x01000000);
      for(unsigned i=17;i<32;i++)assert(cmd[i]==0);
      for(unsigned i=2048;i<A733_UFS_DMA_SIZE;i++)assert(m[i]==0);
    }
  f.regs[0x58/4]=stall_data?1:0;f.regs[0x20/4]=stall_data?0:1;
  rsp[0]=0x21;ufs_le32(m+8,0);
  if(data_commands==bad_command)switch(bad_kind)
    {
      case 0:ufs_le32(m+8,0x100);break;case 1:rsp[0]^=1;break;
      case 2:rsp[1]=0x20;break;case 3:rsp[2]=1;break;case 4:rsp[3]=1;break;
      case 5:rsp[6]=1;break;case 6:rsp[9]=1;break;case 7:rsp[11]=1;break;
      case 8:rsp[33]=255;break;case 9:rsp[15]=1;break;case 10:m[32]=1;break;
      case 11:cmd[0]^=1;break;case 12:m[2048]^=1;break;
      case 13:payload[4000]^=1;break;case 14:f.arena[0]^=1;break;
      case 15:m[A733_UFS_DMA_SIZE]^=1;break;case 16:rsp[7]=2;break;
      case 17:f.regs[0x20/4]=0x10001;break;
    }
  return f.writes==f.fail_write?-EIO:0;
}
static const struct a733_ufs_nop_io dio={rd,dwr,translate,clean,invalidate,now,delay};
static void ready(uint8_t *a)
{
  active=false;setup(a);assert(!prepare(a)&&!test(a));
  memset(&data,0,sizeof(data));memset(volume,0,sizeof(volume));
  data_commands=bad_command=bad_kind=0;stall_data=bad_readback=false;
  base=A733_UFS_DATA_FIRST;active=true;
}
static int enable(uint8_t *a)
{return a733_ufs_data_enable(&data,&s,&r,&board,a,work,&dio,&f);}
static int transfer(uint8_t *a,void *p,uint64_t lba,uint32_t n,bool write)
{return a733_ufs_data_io(&data,&s,&r,&board,a,work,p,lba,n,write,&dio,&f);}
static void existing_fixture(uint8_t *a)
{
  active=false;setup(a);memset(&data,0,sizeof(data));memset(volume,0,sizeof(volume));
  data_commands=bad_command=bad_kind=0;stall_data=bad_readback=false;
  base=A733_UFS_DATA_FIRST;active=true;
  volume[11]=0;volume[12]=16;volume[13]=32;volume[14]=32;volume[16]=2;
  ufs_le32(volume+32,30576479);ufs_le32(volume+36,934);ufs_le32(volume+44,2);
  volume[48]=1;volume[50]=6;volume[510]=0x55;volume[511]=0xaa;
  memcpy(volume+6*4096,volume,4096);
  ufs_le32(volume+4096,0x41615252);ufs_le32(volume+4096+484,0x61417272);
  ufs_le32(volume+4096+508,0xaa550000);
}
int main(void)
{
  uint8_t *a=mmap((void *)0x44000000,A733_UFS_NOP_ARENA,PROT_READ|PROT_WRITE,
    MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);assert(a==(void *)0x44000000);
  for(size_t i=0;i<sizeof(input);i++)input[i]=(uint8_t)(i^(i>>9)^0xa7);
  unsigned faults=0;
  existing_fixture(a);
  assert(!a733_ufs_data_existing(&data,&s,&r,&board,a,work,&dio,&f));
  assert(data.existing_volume&&data.enabled&&!data.writes&&data.flushes==1);
  assert(!s.prepared&&!s.completed&&!write_commands&&!flush_commands);
  assert(!transfer(a,input,16,1,true)); /* Normal writer still verifies+flushes. */
  for(unsigned problem=0;problem<4;problem++)
    {
      existing_fixture(a);
      if(problem==0)volume[510]=0;
      if(problem==1)volume[6*4096+80]^=1;
      if(problem==2)volume[4096+484]^=1;
      if(problem==3)r.model[0]^=1;
      assert(a733_ufs_data_existing(&data,&s,&r,&board,a,work,&dio,&f)<0);
      assert(!data.enabled&&!data.existing_volume&&!data_commands&&!s.prepared);
    }
  /* Recovery never needs scratch qualification and never sends a media
   * WRITE or CACHE command. HCI submission/ack MMIO is still expected. */
  setup(a);assert(!s.completed&&!s.prepared);
  unsigned sw=write_commands,sf=flush_commands;
  assert(!a733_ufs_data_ro_ready(&r,&board,a,&sio,&f));
  assert(!a733_ufs_data_ro_read(&r,&board,a,output,0,1,&sio,&f));
  assert(write_commands==sw&&flush_commands==sf);
  unsigned mm=f.writes;
  assert(a733_ufs_data_ro_read(&r,&board,a,output,A733_UFS_DATA_BLOCKS,1,&sio,&f)==-EINVAL);
  assert(a733_ufs_data_ro_read(&r,&board,a,output,A733_UFS_DATA_BLOCKS-1,2,&sio,&f)==-EINVAL);
  assert(a733_ufs_data_ro_read(&r,&board,a,output,0,257,&sio,&f)==-EINVAL);
  r.model[0]^=1;
  assert(a733_ufs_data_ro_ready(&r,&board,a,&sio,&f)==-EACCES&&f.writes==mm);
  ready(a);unsigned old=f.writes;
  assert(transfer(a,input,0,1,true)==-EACCES&&f.writes==old);
  assert(!enable(a)&&data.enabled&&data.flushes==1);
  assert(enable(a)==-EALREADY);
  for(unsigned n=1;n<=256;n*=16)
    {
      assert(!transfer(a,input,0,n,true)&&!transfer(a,output,0,n,false));
      assert(!memcmp(input,output,n*4096));
    }
  assert(data.verified_bytes==1118208&&data.writes==18&&data.flushes==4);
  for(unsigned where=0;where<3;where++)
    {
      ready(a);base=where==0?A733_UFS_DATA_FIRST:
        where==1?A733_UFS_DATA_FIRST+A733_UFS_DATA_BLOCKS/2:
        A733_UFS_DATA_FIRST+A733_UFS_DATA_BLOCKS-256;
      assert(!enable(a));uint64_t relative=base-A733_UFS_DATA_FIRST;
      assert(!transfer(a,input,relative,256,true)&&!transfer(a,output,relative,256,false));
      assert(!memcmp(input,output,sizeof(input)));
    }
  ready(a);assert(!enable(a));old=f.writes;unsigned reads=f.reads;
  const uint64_t invalid[]={A733_UFS_DATA_BLOCKS,UINT64_MAX,31256496};
  for(unsigned i=0;i<3;i++)assert(transfer(a,input,invalid[i],1,true)==-EINVAL);
  assert(transfer(a,input,A733_UFS_DATA_BLOCKS-1,2,true)==-EINVAL);
  assert(transfer(a,input,0,257,true)==-EINVAL&&transfer(a,input,0,0,true)==-EINVAL);
  assert(transfer(a,a,0,1,true)==-EINVAL&&transfer(a,work,0,1,true)==-EINVAL);
  assert(transfer(a,&data,0,1,true)==-EINVAL);
  assert(transfer(a,(void *)(UINTPTR_MAX-100),0,1,true)==-EINVAL);
  assert(f.writes==old&&f.reads==reads);
  for(unsigned kind=0;kind<8;kind++)
    {
      ready(a);old=f.writes;reads=f.reads;
      switch(kind){case 0:s.completed=false;break;case 1:s.restored=false;break;
        case 2:s.quarantine=true;break;case 3:r.model[0]^=1;break;
        case 4:r.blocks--;break;case 5:r.lun=1;break;
        case 6:r.partitions[2][1]--;break;case 7:s.flushes--;break;}
      assert(enable(a)<0&&!data.enabled&&f.writes==old&&f.reads==reads);++faults;
    }
  for(unsigned command=1;command<=3;command++)for(unsigned kind=0;kind<18;kind++)
    {
      ready(a);bad_command=command;bad_kind=kind;
      int ret=enable(a);if(!ret)ret=transfer(a,input,0,1,true);
      assert(ret<0&&data.quarantine&&r.quarantine&&data_commands==command);
      old=f.writes;assert(transfer(a,input,0,1,true)<0&&f.writes==old);++faults;
    }
  ready(a);assert(!enable(a));bad_readback=true;
  assert(transfer(a,input,0,1,true)==-EBADMSG&&data.writes==1&&data.flushes==1);++faults;
  ready(a);stall_data=true;f.frozen=true;
  assert(enable(a)==-ETIMEDOUT&&data_commands==1&&data.quarantine);++faults;
  /* Fault every write, selected snapshot reads and each translation in a
   * successful write/readback/flush, relative to the completed enable gate. */
  ready(a);assert(!enable(a));unsigned bw=f.writes,br=f.reads,bm=f.maps;
  assert(!transfer(a,input,0,1,true));unsigned ew=f.writes,er=f.reads,em=f.maps;
  for(unsigned k=0;k<3;k++)
    {
      unsigned first=k==0?bw:k==1?br:bm,end=k==0?ew:k==1?er:em;
      for(unsigned p=first+1;p<=end;p+=(k==1?31:1))
        {
          ready(a);assert(!enable(a));f.fault=-EIO;
          if(k==0)f.fail_write=p;else if(k==1)f.fail_read=p;else f.fail_map=p;
          assert(transfer(a,input,0,1,true)<0);
          old=f.writes;assert(transfer(a,input,0,1,true)<0&&f.writes==old);++faults;
        }
    }
  char report[1024];assert(a733_ufs_data_report(&data,report,sizeof(report)));
  assert(strstr(report,"tail-reserved=128"));assert(!munmap(a,A733_UFS_NOP_ARENA));
  printf("data production transport: 4K/64K/1MiB + start/middle/end + %u faults PASS; no hardware\n",faults);
  return 0;
}
