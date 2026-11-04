/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include "a733_ufs_data.h"
#include "a733_ufs_volume.h"

static int fail(struct a733_ufs_data *d,struct a733_ufs_readcheck *r,
                int ret,const char *stage)
{
  d->error=ret<0?ret:-EIO;d->stage=stage;d->quarantine=true;
  r->quarantine=true;r->error=d->error;r->stage=stage;return d->error;
}
static bool apart(uintptr_t a,size_t n,uintptr_t b,size_t k)
{return a<=UINTPTR_MAX-n&&b<=UINTPTR_MAX-k&&(a+n<=b||b+k<=a);}
static bool identity(const struct a733_ufs_readcheck *r)
{
  return !r->lun&&r->blocks==31256576&&r->blocksize==4096&&
    !memcmp(r->model,"KINGSTONCY14-128        0002",28)&&
    r->protective_mbr&&r->gpt_header_valid&&r->gpt_entries_valid&&
    !r->gpt_error&&r->gpt_partitions==3&&r->entry_bytes==16384&&r->entry_lba==2&&
    r->partitions[0][0]==32768&&r->partitions[0][1]==65535&&
    r->partitions[1][0]==65536&&r->partitions[1][1]==679935&&
    r->partitions[2][0]==679936&&r->partitions[2][1]==31256542;
}
static bool proof(const struct a733_ufs_scratch *s,
                  const struct a733_ufs_readcheck *r)
{
  return s->completed&&s->restored&&!s->quarantine&&!s->error&&
    s->stopped&&s->guards&&s->writes==3&&s->flushes==4&&s->patterns==2&&identity(r);
}
int a733_ufs_data_ro_ready(struct a733_ufs_readcheck *r,
  const struct a733_ufs_board *b,uint8_t *arena,
  const struct a733_ufs_nop_io *io,void *ctx)
{
  if(!r||!b||!arena||!io)return -EINVAL;
  if(!identity(r))return -EACCES;
  return a733_ufs_reader_ready(r,b,arena,io,ctx);
}
int a733_ufs_data_ro_read(struct a733_ufs_readcheck *r,
  const struct a733_ufs_board *b,uint8_t *arena,uint8_t *buffer,
  uint64_t start,uint32_t count,const struct a733_ufs_nop_io *io,void *ctx)
{
  if(!buffer||!count||count>256||start>=A733_UFS_DATA_BLOCKS||
     count>A733_UFS_DATA_BLOCKS-start)return -EINVAL;
  int ret=a733_ufs_data_ro_ready(r,b,arena,io,ctx);if(ret)return ret;
  return a733_ufs_reader_read(r,b,arena,buffer,A733_UFS_DATA_FIRST+start,count,io,ctx);
}
static int gate(struct a733_ufs_data *d,struct a733_ufs_scratch *s,
  struct a733_ufs_readcheck *r,const struct a733_ufs_board *b,
  uint8_t *arena,uint8_t *work,const struct a733_ufs_nop_io *io,void *ctx)
{
  if(!d||!s||!r||!b||!arena||!work||!io)return -EINVAL;
  uintptr_t p[5]={(uintptr_t)d,(uintptr_t)s,(uintptr_t)r,
                  (uintptr_t)arena,(uintptr_t)work};
  size_t n[5]={sizeof(*d),sizeof(*s),sizeof(*r),A733_UFS_NOP_ARENA,65536};
  for(unsigned i=0;i<5;i++)for(unsigned j=i+1;j<5;j++)
    if(!apart(p[i],n[i],p[j],n[j]))return -EINVAL;
  if(d->quarantine||s->quarantine||s->error||
     (!proof(s,r)&&!(d->existing_volume&&identity(r))))return -EACCES;
  return a733_ufs_reader_ready(r,b,arena,io,ctx);
}
static int snapshot(struct a733_ufs_data *d,struct a733_ufs_readcheck *r,
  const struct a733_ufs_board *b,const struct a733_ufs_nop_io *io,void *ctx,
  uint32_t run,uint32_t is)
{
  int ret=a733_ufs_handoff_snapshot(&r->after,b,io->read,ctx);
  if(ret)return fail(d,r,ret,"data-snapshot-failed");
  const uint32_t *v=r->after.raw;
  if(!r->after.stable||!r->after.link_ready||r->after.pending||v[0]!=0x11870f1f||
     v[1]!=0x300||v[2]||v[3]!=is||v[4]||v[5]!=0x10f||v[6]!=1||v[7]||
     v[8]!=(uint32_t)r->physical||v[9]!=r->physical>>32||v[11]!=run||v[12]||v[13]||v[15])
    return fail(d,r,-EBUSY,"data-unexpected-hci-state");
  return 0;
}
static int write_reg(struct a733_ufs_data *d,struct a733_ufs_readcheck *r,
  const struct a733_ufs_nop_io *io,void *ctx,uint32_t off,uint32_t value)
{
  ++d->mmio;int ret=io->write(ctx,off,value);
  return ret?fail(d,r,ret,"data-mmio-failed"):0;
}
static int wait_reg(struct a733_ufs_data *d,struct a733_ufs_readcheck *r,
  const struct a733_ufs_nop_io *io,void *ctx,uint32_t off,uint32_t expect,bool flush)
{
  unsigned limit=off==0x58?(flush?10000:3000):20;
  uint32_t start=io->now_ms(ctx),value,is;
  for(unsigned i=0;i<limit*10;i++)
    {
      ++d->polls;int ret=io->read(ctx,0x20,&is);
      if(ret)return fail(d,r,ret,"data-event-read-failed");
      if(is&~UINT32_C(1))return fail(d,r,-EIO,"data-controller-event");
      ret=io->read(ctx,off,&value);
      if(ret)return fail(d,r,ret,"data-poll-read-failed");
      if(off==0x58&&(value&~UINT32_C(1)))return fail(d,r,-EBUSY,"data-unowned-doorbell");
      if(io->now_ms(ctx)-start>limit)return fail(d,r,-ETIMEDOUT,"data-deadline");
      if(value==expect)return 0;
      io->delay_100us(ctx);
    }
  return fail(d,r,-ETIMEDOUT,"data-poll-bound");
}
/* Independent constrained data writer. The v172 fixed-window encoder/test is
 * unchanged. Build its proven packet privately, then replace all variable
 * length/LBA fields BEFORE cleaning/submitting; never issue the scratch CDB. */
static int exchange(struct a733_ufs_data *d,struct a733_ufs_scratch *s,
  struct a733_ufs_readcheck *r,const struct a733_ufs_board *b,uint8_t *arena,
  uint8_t *work,const uint8_t *payload,uint64_t lba,uint32_t count,
  const struct a733_ufs_nop_io *io,void *ctx)
{
  bool flush=payload==NULL;
  if((!flush&&(!count||count>16||lba<A733_UFS_DATA_FIRST||
      lba>=A733_UFS_DATA_FIRST+A733_UFS_DATA_BLOCKS||
      count>A733_UFS_DATA_FIRST+A733_UFS_DATA_BLOCKS-lba))||
     (flush&&(lba||count)))return -EINVAL;
  int ret=gate(d,s,r,b,arena,work,io,ctx);
  if(ret)return fail(d,r,ret,"data-live-gate-failed");
  if(io->now_ms(ctx)-d->start_ms>60000)return fail(d,r,-ETIMEDOUT,"data-batch-deadline");
  size_t bytes=(size_t)count*4096;
  uint8_t *m=arena+4096,*cmd=m+A733_UFS_UCD_OFFSET,*rsp=m+A733_UFS_RSP_OFFSET;
  ret=ufs_scratch_command(m,r->physical,flush);
  if(ret)return fail(d,r,ret,"data-encode-failed");
  if(!flush)
    {
      ufs_be32(cmd+12,(uint32_t)bytes);
      ufs_be32(cmd+18,lba>>32);ufs_be32(cmd+22,(uint32_t)lba);
      ufs_be32(cmd+26,count);ufs_le32(m+A733_UFS_PRD_OFFSET+12,(uint32_t)bytes-1);
      memcpy(m+A733_UFS_DATA_OFFSET,payload,bytes);
    }
  memcpy(d->descriptor,m,32);memcpy(d->request,cmd,32);
  memcpy(d->prdt,m+A733_UFS_PRD_OFFSET,16);
  io->clean(ctx,(uintptr_t)arena,A733_UFS_NOP_ARENA);
  if((ret=write_reg(d,r,io,ctx,0x20,1))||(ret=snapshot(d,r,b,io,ctx,0,0)))return ret;
  r->stopped=false;r->guards=false;
  if((ret=write_reg(d,r,io,ctx,0x60,1))||(ret=wait_reg(d,r,io,ctx,0x60,1,flush))||
     (ret=snapshot(d,r,b,io,ctx,1,0)))return ret;
  if(flush)++d->flushes;else ++d->writes;
  if((ret=write_reg(d,r,io,ctx,0x58,1))||(ret=wait_reg(d,r,io,ctx,0x58,0,flush))||
     (ret=snapshot(d,r,b,io,ctx,1,1)))return ret;
  io->invalidate(ctx,(uintptr_t)arena,A733_UFS_NOP_ARENA);
  for(size_t i=0;i<4096;i++)
    if(arena[i]!=0xa5||m[A733_UFS_DMA_SIZE+i]!=0x5a)
      return fail(d,r,-EFAULT,"data-dma-guard-corrupt");
  r->guards=true;
  if(ufs_getle32(m+8))return fail(d,r,-EIO,"data-ocs-rejected");
  for(size_t i=0;i<A733_UFS_DMA_SIZE;i++)
    {
      if(i>=A733_UFS_RSP_OFFSET&&i<A733_UFS_PRD_OFFSET)continue;
      uint8_t expected=i<32?((i>=8&&i<12)?0:d->descriptor[i]):
        i>=A733_UFS_UCD_OFFSET&&i<A733_UFS_UCD_OFFSET+32?d->request[i-A733_UFS_UCD_OFFSET]:
        i>=A733_UFS_PRD_OFFSET&&i<A733_UFS_PRD_OFFSET+16?d->prdt[i-A733_UFS_PRD_OFFSET]:
        i>=A733_UFS_DATA_OFFSET&&i<A733_UFS_DATA_OFFSET+bytes&&payload?payload[i-A733_UFS_DATA_OFFSET]:0;
      if(m[i]!=expected)return fail(d,r,-EBADMSG,"data-unexpected-dma-mutation");
    }
  unsigned segment=(unsigned)rsp[10]<<8|rsp[11],sense=(unsigned)rsp[32]<<8|rsp[33];
  if(rsp[0]!=0x21||rsp[1]||rsp[2]||rsp[3]||rsp[6]||rsp[8]||rsp[9]||
     segment>480||sense>18||ufs_getbe32(rsp+12))
    return fail(d,r,-EPROTO,"data-response-framing");
  if(rsp[7])return fail(d,r,-EREMOTEIO,"data-scsi-error-NO-retry");
  if(segment||sense)return fail(d,r,-EPROTO,"data-unexpected-response-data");
  if((ret=write_reg(d,r,io,ctx,0x60,0))||(ret=wait_reg(d,r,io,ctx,0x60,0,flush))||
     (ret=snapshot(d,r,b,io,ctx,0,1)))return ret;
  r->stopped=true;return 0;
}
int a733_ufs_data_enable(struct a733_ufs_data *d,struct a733_ufs_scratch *s,
  struct a733_ufs_readcheck *r,const struct a733_ufs_board *b,uint8_t *arena,
  uint8_t *work,const struct a733_ufs_nop_io *io,void *ctx)
{
  int ret=gate(d,s,r,b,arena,work,io,ctx);if(ret)return ret;
  if(d->enabled)return -EALREADY;
  d->start_ms=io->now_ms(ctx);
  ret=exchange(d,s,r,b,arena,work,NULL,0,0,io,ctx);if(ret)return ret;
  d->enabled=true;d->stage="data-enabled-NOT-formatted-or-mounted";return 0;
}
int a733_ufs_data_existing(struct a733_ufs_data *d,struct a733_ufs_scratch *s,
  struct a733_ufs_readcheck *r,const struct a733_ufs_board *b,uint8_t *arena,
  uint8_t *work,const struct a733_ufs_nop_io *io,void *ctx)
{
  if(!d||!s||!r||!b||!arena||!work||!io)return -EINVAL;
  if(d->enabled)return -EALREADY;
  if(d->quarantine||s->quarantine||s->error)return -EACCES;
  /* Validate object/buffer separation without authorizing any writes. */
  uintptr_t objects[5]={(uintptr_t)d,(uintptr_t)s,(uintptr_t)r,
                       (uintptr_t)arena,(uintptr_t)work};
  size_t sizes[5]={sizeof(*d),sizeof(*s),sizeof(*r),A733_UFS_NOP_ARENA,65536};
  for(unsigned i=0;i<5;i++)for(unsigned j=i+1;j<5;j++)
    if(!apart(objects[i],sizes[i],objects[j],sizes[j]))return -EINVAL;
  int ret=a733_ufs_data_ro_read(r,b,arena,work,0,1,io,ctx);
  unsigned info=0,backup=0;
  if(!ret)ret=ufs_volume_boot(work,(uint32_t)A733_UFS_DATA_BLOCKS,&info,&backup);
  if(!ret)ret=a733_ufs_data_ro_read(r,b,arena,work+4096,backup,1,io,ctx);
  if(!ret&&memcmp(work,work+4096,4096))ret=-EBADMSG;
  if(!ret)ret=a733_ufs_data_ro_read(r,b,arena,work+4096,info,1,io,ctx);
  if(!ret)ret=ufs_volume_info(work+4096);
  if(ret){d->stage="data-existing-FAT-rejected-NO-format";d->error=ret;return ret;}
  d->existing_volume=true;
  ret=a733_ufs_data_enable(d,s,r,b,arena,work,io,ctx);
  if(ret){d->existing_volume=false;return ret;}
  d->stage="data-existing-FAT-qualified";return 0;
}
int a733_ufs_data_flush(struct a733_ufs_data *d,struct a733_ufs_scratch *s,
  struct a733_ufs_readcheck *r,const struct a733_ufs_board *b,uint8_t *arena,
  uint8_t *work,const struct a733_ufs_nop_io *io,void *ctx)
{
  int ret=gate(d,s,r,b,arena,work,io,ctx);if(ret)return ret;
  if(!d->enabled)return -EACCES;
  d->start_ms=io->now_ms(ctx);
  ret=exchange(d,s,r,b,arena,work,NULL,0,0,io,ctx);
  if(!ret)d->stage="data-flush-complete";
  return ret;
}
int a733_ufs_data_io(struct a733_ufs_data *d,struct a733_ufs_scratch *s,
  struct a733_ufs_readcheck *r,const struct a733_ufs_board *b,uint8_t *arena,
  uint8_t *work,void *buffer,uint64_t start,uint32_t count,bool writing,
  const struct a733_ufs_nop_io *io,void *ctx)
{
  /* Reject any out-of-view request or alias before HCI/MMIO, even if disabled. */
  if(!d||!s||!r||!b||!arena||!work||!io||!buffer||!count||count>256||
     start>=A733_UFS_DATA_BLOCKS||count>A733_UFS_DATA_BLOCKS-start)return -EINVAL;
  size_t bytes=(size_t)count*4096;uintptr_t p=(uintptr_t)buffer;
  if(!apart(p,bytes,(uintptr_t)d,sizeof(*d))||
     !apart(p,bytes,(uintptr_t)s,sizeof(*s))||!apart(p,bytes,(uintptr_t)r,sizeof(*r))||
     !apart(p,bytes,(uintptr_t)arena,A733_UFS_NOP_ARENA)||
     !apart(p,bytes,(uintptr_t)work,65536))return -EINVAL;
  if(!d->enabled)return -EACCES;
  int ret=gate(d,s,r,b,arena,work,io,ctx);if(ret)return ret;
  d->start_ms=io->now_ms(ctx);
  if(!writing)
    {
      ret=a733_ufs_reader_read(r,b,arena,buffer,A733_UFS_DATA_FIRST+start,count,io,ctx);
      if(ret)return fail(d,r,ret,"data-read-failed");
      d->read_bytes+=bytes;d->stage="data-read-complete";return 0;
    }
  for(uint32_t done=0;done<count;)
    {
      uint32_t chunk=count-done;if(chunk>16)chunk=16;
      const uint8_t *src=(const uint8_t *)buffer+(size_t)done*4096;
      uint64_t lba=A733_UFS_DATA_FIRST+start+done;
      ret=exchange(d,s,r,b,arena,work,src,lba,chunk,io,ctx);if(ret)return ret;
      ret=a733_ufs_reader_read(r,b,arena,work,lba,chunk,io,ctx);
      if(ret)return fail(d,r,ret,"data-verify-read-failed");
      if(memcmp(work,src,(size_t)chunk*4096))return fail(d,r,-EBADMSG,"data-readback-mismatch");
      d->verified_bytes+=(size_t)chunk*4096;done+=chunk;
    }
  ret=exchange(d,s,r,b,arena,work,NULL,0,0,io,ctx);
  if(!ret)d->stage="data-write-verified-and-flushed";
  return ret;
}
size_t a733_ufs_data_report(const struct a733_ufs_data *d,char *out,size_t cap)
{
  if(!d||!out||!cap)return 0;
  int n=snprintf(out,cap,
    "data-v175: enabled=%u quarantine=%u error=%d stage=%s node=/dev/ufsdata existing-FAT=%u\n"
    "data-view: lun=0 first=679936 blocks=30576479 block-bytes=4096 tail-reserved=128; /dev/ufs0 STILL readonly\n"
    "data-proof: writes=%" PRIu64 " flushes=%" PRIu64 " verified-bytes=%" PRIu64 " read-bytes=%" PRIu64 " polls=%" PRIu32 " mmio=%" PRIu32 "\n"
    "data-policy: data-existing checks existing FAT for automount; data-rw=ERASE-TEST-LINUX retains manual scratch path; NO automatic format/migration/GPT edits/reset/retry\n",
    d->enabled,d->quarantine,d->error,d->stage?d->stage:"disabled",d->existing_volume,
    d->writes,d->flushes,d->verified_bytes,d->read_bytes,d->polls,d->mmio);
  if(n<0){out[0]=0;return 0;}return (size_t)n<cap?(size_t)n:cap-1;
}
