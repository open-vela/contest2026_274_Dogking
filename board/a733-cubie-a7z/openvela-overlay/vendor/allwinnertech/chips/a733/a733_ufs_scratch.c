/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include "a733_ufs_scratch.h"
#include "a733_ufs_write_wire.h"

static uint32_t scrc(const uint8_t *p,size_t n)
{
  uint32_t c=UINT32_MAX;
  for(size_t i=0;i<n;i++)
    {c^=p[i];for(unsigned j=0;j<8;j++)c=(c>>1)^((0-(c&1))&0xedb88320u);}
  return ~c;
}
static int sfail(struct a733_ufs_scratch *s,struct a733_ufs_readcheck *r,
                 int ret,const char *stage)
{
  s->error=ret<0?ret:-EIO;s->stage=stage;s->quarantine=true;
  r->quarantine=true;r->error=s->error;r->stage=stage;return s->error;
}
static bool separate(uintptr_t a,size_t n,uintptr_t b,size_t k)
{return a<=UINTPTR_MAX-n&&b<=UINTPTR_MAX-k&&(a+n<=b||b+k<=a);}
static int sgate(struct a733_ufs_scratch *s,struct a733_ufs_readcheck *r,
                 const struct a733_ufs_board *b,uint8_t *arena,uint8_t *work,
                 const struct a733_ufs_nop_io *io,void *ctx)
{
  if(!s||!r||!work||!arena||!b||!io)return -EINVAL;
  uintptr_t sa=(uintptr_t)s,ra=(uintptr_t)r,wa=(uintptr_t)work,aa=(uintptr_t)arena;
  if(!separate(sa,sizeof(*s),ra,sizeof(*r))||
     !separate(sa,sizeof(*s),aa,A733_UFS_NOP_ARENA)||
     !separate(wa,65536,sa,sizeof(*s))||!separate(wa,65536,ra,sizeof(*r))||
     !separate(wa,65536,aa,A733_UFS_NOP_ARENA))return -EINVAL;
  if(s->quarantine||r->lun||r->blocks!=31256576||r->blocksize!=4096||
     memcmp(r->model,"KINGSTONCY14-128        0002",28)||
     !r->protective_mbr||!r->gpt_header_valid||!r->gpt_entries_valid||
     r->gpt_error||r->gpt_partitions!=3||r->entry_bytes!=16384||
     r->entry_lba!=2||r->partitions[0][0]!=32768||r->partitions[0][1]!=65535||
     r->partitions[1][0]!=65536||r->partitions[1][1]!=679935||
     r->partitions[2][0]!=679936||r->partitions[2][1]!=31256542||
     ((r->partitions[2][1]-32)&~UINT64_C(15))!=A733_UFS_SCRATCH_LBA)
    return -EACCES;
  return a733_ufs_reader_ready(r,b,arena,io,ctx);
}
static int sread(struct a733_ufs_scratch *s,struct a733_ufs_readcheck *r,
                  const struct a733_ufs_board *b,uint8_t *arena,uint8_t *out,
                  uint64_t lba,uint32_t count,const struct a733_ufs_nop_io *io,void *ctx)
{
  if(io->now_ms(ctx)-s->start_ms>60000)
    return sfail(s,r,-ETIMEDOUT,"scratch-batch-deadline");
  int ret=a733_ufs_reader_read(r,b,arena,out,lba,count,io,ctx);
  return ret?sfail(s,r,ret,"scratch-read-failed"):0;
}
static int smetadata(struct a733_ufs_scratch *s,struct a733_ufs_readcheck *r,
                      const struct a733_ufs_board *b,uint8_t *arena,uint8_t *work,
                      const struct a733_ufs_nop_io *io,void *ctx)
{
  for(unsigned i=0;i<3;i++)
    {
      int ret=sread(s,r,b,arena,work,r->lbas[i],4,io,ctx);if(ret)return ret;
      if(scrc(work,16384)!=r->crc[i]||r->crc[i]!=r->repeat_crc[i])
        return sfail(s,r,-EBADMSG,"scratch-media-sample-changed");
    }
  int ret=sread(s,r,b,arena,work,r->entry_lba,4,io,ctx);if(ret)return ret;
  return scrc(work,16384)==r->entry_crc?0:sfail(s,r,-EBADMSG,"scratch-gpt-changed");
}
static int sneighbors(struct a733_ufs_scratch *s,struct a733_ufs_readcheck *r,
                       const struct a733_ufs_board *b,uint8_t *arena,uint8_t *work,
                       const struct a733_ufs_nop_io *io,void *ctx)
{
  for(unsigned i=0;i<2;i++)
    {
      uint64_t lba=A733_UFS_SCRATCH_LBA+(i?16:UINT64_MAX);
      int ret=sread(s,r,b,arena,work,lba,1,io,ctx);if(ret)return ret;
      if(memcmp(work,s->neighbors+4096*i,4096))
        return sfail(s,r,-EBADMSG,"scratch-neighbor-changed");
    }
  return 0;
}
int a733_ufs_scratch_prepare(struct a733_ufs_scratch *s,
  struct a733_ufs_readcheck *r,const struct a733_ufs_board *b,uint8_t *arena,uint8_t *work,
  const struct a733_ufs_nop_io *io,void *ctx)
{
  int ret=sgate(s,r,b,arena,work,io,ctx);if(ret)return ret;
  if(s->prepare_attempted)return -EALREADY;
  s->prepare_attempted=true;s->start_ms=io->now_ms(ctx);s->stage="scratch-backup-reading";
  if((ret=smetadata(s,r,b,arena,work,io,ctx)))return ret;
  if((ret=sread(s,r,b,arena,s->original,A733_UFS_SCRATCH_LBA,16,io,ctx)))return ret;
  for(unsigned i=0;i<2;i++)
    if((ret=sread(s,r,b,arena,s->neighbors+4096*i,
                 A733_UFS_SCRATCH_LBA+(i?16:UINT64_MAX),1,io,ctx)))return ret;
  if((ret=sread(s,r,b,arena,work,A733_UFS_SCRATCH_LBA,16,io,ctx)))return ret;
  if(memcmp(work,s->original,65536))return sfail(s,r,-EBADMSG,"scratch-backup-repeat-mismatch");
  if((ret=sneighbors(s,r,b,arena,work,io,ctx)))return ret;
  s->original_crc=scrc(s->original,65536);s->prepared=true;s->stopped=true;s->guards=true;
  s->elapsed_ms=io->now_ms(ctx)-s->start_ms;s->stage="scratch-prepared-RAM-backup-only";
  return 0;
}
static int ssnapshot(struct a733_ufs_scratch *s,struct a733_ufs_readcheck *r,
  const struct a733_ufs_board *b,const struct a733_ufs_nop_io *io,void *ctx,uint32_t run,uint32_t is)
{
  int ret=a733_ufs_handoff_snapshot(&r->after,b,io->read,ctx);
  if(ret)return sfail(s,r,ret,"scratch-snapshot-failed");
  const uint32_t *v=r->after.raw;
  if(!r->after.stable||!r->after.link_ready||r->after.pending||v[0]!=0x11870f1f||
     v[1]!=0x300||v[2]||v[3]!=is||v[4]||v[5]!=0x10f||v[6]!=1||v[7]||
     v[8]!=(uint32_t)r->physical||v[9]!=r->physical>>32||v[11]!=run||v[12]||v[13]||v[15])
    return sfail(s,r,-EBUSY,"scratch-unexpected-hci-state");
  return 0;
}
static int swrite(struct a733_ufs_scratch *s,struct a733_ufs_readcheck *r,
  const struct a733_ufs_nop_io *io,void *ctx,uint32_t offset,uint32_t value)
{
  ++s->mmio;int ret=io->write(ctx,offset,value);
  return ret?sfail(s,r,ret,"scratch-mmio-failed"):0;
}
static int swait(struct a733_ufs_scratch *s,struct a733_ufs_readcheck *r,
  const struct a733_ufs_nop_io *io,void *ctx,uint32_t offset,uint32_t expected,bool flush)
{
  uint32_t start=io->now_ms(ctx),value,is;
  unsigned timeout=offset==0x58?(flush?10000:3000):20;
  for(unsigned i=0;i<timeout*10;i++)
    {
      ++s->polls;int ret=io->read(ctx,0x20,&is);
      if(ret)return sfail(s,r,ret,"scratch-event-read-failed");
      if(is&~UINT32_C(1))return sfail(s,r,-EIO,"scratch-controller-event");
      ret=io->read(ctx,offset,&value);
      if(ret)return sfail(s,r,ret,"scratch-poll-read-failed");
      if(offset==0x58&&(value&~UINT32_C(1)))return sfail(s,r,-EBUSY,"scratch-unowned-doorbell");
      if(io->now_ms(ctx)-start>timeout)return sfail(s,r,-ETIMEDOUT,"scratch-deadline");
      if(value==expected)return 0;
      io->delay_100us(ctx);
    }
  return sfail(s,r,-ETIMEDOUT,"scratch-poll-bound");
}
static int sexchange(struct a733_ufs_scratch *s,struct a733_ufs_readcheck *r,
  const struct a733_ufs_board *b,uint8_t *arena,uint8_t *work,const uint8_t *payload,
  const struct a733_ufs_nop_io *io,void *ctx)
{
  bool flush=payload==NULL;
  if(s->writes>=3&&!flush)return sfail(s,r,-E2BIG,"scratch-write-bound");
  if(s->flushes>=4&&flush)return sfail(s,r,-E2BIG,"scratch-flush-bound");
  int ret=sgate(s,r,b,arena,work,io,ctx);
  if(ret)return sfail(s,r,ret,"scratch-live-gate-failed");
  if(io->now_ms(ctx)-s->start_ms>60000)return sfail(s,r,-ETIMEDOUT,"scratch-batch-deadline");
  uint8_t *m=arena+4096,*rsp=m+A733_UFS_RSP_OFFSET;
  ret=ufs_scratch_command(m,r->physical,flush);if(ret)return sfail(s,r,ret,"scratch-encode-failed");
  if(payload)memcpy(m+A733_UFS_DATA_OFFSET,payload,65536);
  memcpy(s->descriptor,m,32);memcpy(s->request,m+A733_UFS_UCD_OFFSET,32);
  memcpy(s->prdt,m+A733_UFS_PRD_OFFSET,16);
  io->clean(ctx,(uintptr_t)arena,A733_UFS_NOP_ARENA);
  if((ret=swrite(s,r,io,ctx,0x20,1))||(ret=ssnapshot(s,r,b,io,ctx,0,0)))return ret;
  s->stopped=false;r->stopped=false;s->guards=false;r->guards=false;
  if((ret=swrite(s,r,io,ctx,0x60,1))||(ret=swait(s,r,io,ctx,0x60,1,flush))||
     (ret=ssnapshot(s,r,b,io,ctx,1,0)))return ret;
  if(flush)++s->flushes;else ++s->writes;
  s->submitted=true;
  if((ret=swrite(s,r,io,ctx,0x58,1))||(ret=swait(s,r,io,ctx,0x58,0,flush))||
     (ret=ssnapshot(s,r,b,io,ctx,1,1)))return ret;
  io->invalidate(ctx,(uintptr_t)arena,A733_UFS_NOP_ARENA);
  s->ocs=ufs_getle32(m+8);memcpy(s->response,rsp,64);s->status=rsp[7];
  for(size_t i=0;i<4096;i++)
    if(arena[i]!=0xa5||m[A733_UFS_DMA_SIZE+i]!=0x5a)
      return sfail(s,r,-EFAULT,"scratch-dma-guard-corrupt");
  s->guards=true;r->guards=true;
  if(s->ocs)return sfail(s,r,-EIO,"scratch-ocs-rejected");
  for(size_t i=0;i<A733_UFS_DMA_SIZE;i++)
    {
      if(i>=A733_UFS_RSP_OFFSET&&i<A733_UFS_PRD_OFFSET)continue;
      uint8_t expected=i<32?((i>=8&&i<12)?0:s->descriptor[i]):
        i>=A733_UFS_UCD_OFFSET&&i<A733_UFS_UCD_OFFSET+32?s->request[i-A733_UFS_UCD_OFFSET]:
        i>=A733_UFS_PRD_OFFSET&&i<A733_UFS_PRD_OFFSET+16?s->prdt[i-A733_UFS_PRD_OFFSET]:
        i>=A733_UFS_DATA_OFFSET&&payload?payload[i-A733_UFS_DATA_OFFSET]:0;
      if(m[i]!=expected)return sfail(s,r,-EBADMSG,"scratch-unexpected-dma-mutation");
    }
  unsigned segment=(unsigned)rsp[10]<<8|rsp[11],sense=(unsigned)rsp[32]<<8|rsp[33];
  if(rsp[0]!=0x21||rsp[1]||rsp[2]||rsp[3]||rsp[6]||rsp[8]||rsp[9]||
     segment>480||sense>18||ufs_getbe32(rsp+12))
    return sfail(s,r,-EPROTO,"scratch-response-framing");
  if(rsp[7])
    {
      if(sense>=14&&segment>=sense+2&&(rsp[34]&0x7f)==0x70)
        {s->sense_key=rsp[36]&15;s->asc=rsp[46];s->ascq=rsp[47];}
      else if(sense>=8&&segment>=sense+2&&(rsp[34]&0x7e)==0x72)
        {s->sense_key=rsp[35]&15;s->asc=rsp[36];s->ascq=rsp[37];}
      return sfail(s,r,-EREMOTEIO,"scratch-scsi-error-NO-retry-or-restore");
    }
  if(segment||sense)return sfail(s,r,-EPROTO,"scratch-unexpected-response-data");
  if((ret=swrite(s,r,io,ctx,0x60,0))||(ret=swait(s,r,io,ctx,0x60,0,flush))||
     (ret=ssnapshot(s,r,b,io,ctx,0,1)))return ret;
  s->stopped=true;r->stopped=true;return 0;
}
static uint8_t pattern(unsigned pass,size_t i)
{
  uint32_t v=(uint32_t)i*0x9e3779b9u+0x85ebca6bu;
  v^=v>>13;v*=0xc2b2ae35u;v^=v>>16;
  return (uint8_t)(v>>(8*(i&3)))^(pass?0xa5:0x5a);
}
int a733_ufs_scratch_test(struct a733_ufs_scratch *s,
  struct a733_ufs_readcheck *r,const struct a733_ufs_board *b,uint8_t *arena,uint8_t *work,
  const struct a733_ufs_nop_io *io,void *ctx)
{
  int ret=sgate(s,r,b,arena,work,io,ctx);if(ret)return ret;
  if(!s->prepared)return -EACCES;
  if(s->attempted)return -EALREADY;
  s->attempted=true;s->start_ms=io->now_ms(ctx);s->stage="scratch-prewrite-recheck";
  if(scrc(s->original,65536)!=s->original_crc)return sfail(s,r,-EBADMSG,"scratch-RAM-backup-corrupt");
  if((ret=smetadata(s,r,b,arena,work,io,ctx))||
     (ret=sneighbors(s,r,b,arena,work,io,ctx))||
     (ret=sread(s,r,b,arena,work,A733_UFS_SCRATCH_LBA,16,io,ctx)))return ret;
  if(memcmp(work,s->original,65536))return sfail(s,r,-EBADMSG,"scratch-media-changed-before-write");
  s->stage="scratch-prewrite-flush-qualification";
  if((ret=sexchange(s,r,b,arena,work,NULL,io,ctx))||
     (ret=sneighbors(s,r,b,arena,work,io,ctx))||
     (ret=sread(s,r,b,arena,work,A733_UFS_SCRATCH_LBA,16,io,ctx)))return ret;
  if(memcmp(work,s->original,65536))return sfail(s,r,-EBADMSG,"scratch-media-changed-during-preflush");
  for(unsigned pass=0;pass<2;pass++)
    {
      s->stage="scratch-pattern-write-flush-read";
      for(size_t i=0;i<65536;i++)work[i]=pattern(pass,i);
      if((ret=sexchange(s,r,b,arena,work,work,io,ctx))||
         (ret=sexchange(s,r,b,arena,work,NULL,io,ctx))||
         (ret=sread(s,r,b,arena,work,A733_UFS_SCRATCH_LBA,16,io,ctx)))return ret;
      for(size_t i=0;i<65536;i++)
        if(work[i]!=pattern(pass,i))return sfail(s,r,-EBADMSG,"scratch-pattern-readback-mismatch");
      if((ret=sneighbors(s,r,b,arena,work,io,ctx)))return ret;
      ++s->patterns;
    }
  s->stage="scratch-original-restore-flush-read";
  if(scrc(s->original,65536)!=s->original_crc)return sfail(s,r,-EBADMSG,"scratch-RAM-backup-corrupt-before-restore");
  if((ret=sexchange(s,r,b,arena,work,s->original,io,ctx))||
     (ret=sexchange(s,r,b,arena,work,NULL,io,ctx))||
     (ret=sread(s,r,b,arena,work,A733_UFS_SCRATCH_LBA,16,io,ctx)))return ret;
  if(memcmp(work,s->original,65536))return sfail(s,r,-EBADMSG,"scratch-original-restore-mismatch");
  s->restored=true;
  if((ret=sneighbors(s,r,b,arena,work,io,ctx))||
     (ret=smetadata(s,r,b,arena,work,io,ctx)))return ret;
  s->completed=true;s->elapsed_ms=io->now_ms(ctx)-s->start_ms;
  s->stage="scratch-write-flush-read-restore-passed";return 0;
}
size_t a733_ufs_scratch_report(const struct a733_ufs_scratch *s,char *out,size_t cap)
{
  if(!s||!out||!cap)return 0;
  int n=snprintf(out,cap,
    "scratch-v172: prepared=%u attempted=%u completed=%u restored=%u quarantine=%u error=%d stage=%s\n"
    "scratch-window: lun=0 lba=31256496..31256511 bytes=65536 inside-disposable-Linux-partition=3; NOT unused-space\n"
    "scratch-proof: writes=%" PRIu32 " flushes=%" PRIu32 " patterns=%" PRIu32 " stopped=%u guards=%u elapsed-ms=%" PRIu32 " polls=%" PRIu32 " mmio=%" PRIu32 "\n"
    "scratch-backup: node=/dev/a733-ufs-backup bytes=%u original-crc32=%08" PRIx32 " volatile-RAM=1 lost-on-reboot=1\n"
    "scratch-sense: ocs=%08" PRIx32 " status=%02x key=%02x asc=%02x ascq=%02x\n"
    "scratch-policy: manual scratch-prepare then scratch-test=ERASE-TEST-LINUX; no failure retry/reset/automatic recovery write; power loss CAN destroy test Linux data\n",
    s->prepared,s->attempted,s->completed,s->restored,s->quarantine,s->error,s->stage?s->stage:"not-attempted",
    s->writes,s->flushes,s->patterns,s->stopped,s->guards,s->elapsed_ms,s->polls,s->mmio,
    s->prepared?65536u:0u,s->original_crc,s->ocs,s->status,s->sense_key,s->asc,s->ascq);
  if(n<0){out[0]=0;return 0;}return (size_t)n<cap?(size_t)n:cap-1;
}
