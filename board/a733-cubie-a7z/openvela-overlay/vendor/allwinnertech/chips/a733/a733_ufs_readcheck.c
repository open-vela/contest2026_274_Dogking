/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include "a733_ufs_readcheck.h"

static int rfail(struct a733_ufs_readcheck *s,int error,const char *stage)
{
  s->error=error<0?error:-EPROTO;s->stage=stage;
  s->quarantine=s->writes!=0;return s->error;
}
static int rsnapshot(struct a733_ufs_readcheck *s,const struct a733_ufs_board *b,
                     const struct a733_ufs_nop_io *io,void *ctx,
                     uint64_t base,uint32_t run,uint32_t is)
{
  int ret=a733_ufs_handoff_snapshot(&s->after,b,io->read,ctx);
  if(ret)return rfail(s,ret,"readcheck-snapshot-failed");
  if(!s->after.stable||!s->after.link_ready||s->after.pending||
     s->after.raw[0]!=0x11870f1f||s->after.raw[1]!=0x300||s->after.raw[2]||
     s->after.raw[3]!=is||s->after.raw[4]||s->after.raw[5]!=0x10f||
     s->after.raw[6]!=1||s->after.raw[7]||
     s->after.raw[8]!=(uint32_t)base||s->after.raw[9]!=base>>32||
     s->after.raw[11]!=run||s->after.raw[12]||s->after.raw[13]||s->after.raw[15])
    return rfail(s,-EBUSY,"readcheck-unexpected-hci-state");
  return 0;
}
static int rwrite(struct a733_ufs_readcheck *s,const struct a733_ufs_nop_io *io,
                  void *ctx,uint32_t offset,uint32_t value)
{
  ++s->writes;int ret=io->write(ctx,offset,value);
  return ret?rfail(s,ret,"readcheck-write-failed"):0;
}
static int rwait(struct a733_ufs_readcheck *s,const struct a733_ufs_nop_io *io,
                 void *ctx,uint32_t offset,uint32_t expected)
{
  uint32_t start=io->now_ms(ctx),value,is;
  unsigned limit=offset==0x58?30000:200,timeout=offset==0x58?3000:20;
  for(unsigned i=0;i<limit;i++)
    {
      ++s->polls;int ret=io->read(ctx,0x20,&is);
      if(ret)return rfail(s,ret,"readcheck-event-read-failed");
      if(is&~UINT32_C(1))return rfail(s,-EIO,"readcheck-controller-event");
      ret=io->read(ctx,offset,&value);
      if(ret)return rfail(s,ret,"readcheck-poll-read-failed");
      if(offset==0x58&&(value&~UINT32_C(1)))
        return rfail(s,-EBUSY,"readcheck-unowned-doorbell");
      if(io->now_ms(ctx)-start>timeout)return rfail(s,-ETIMEDOUT,"readcheck-deadline");
      if(value==expected)return 0;
      io->delay_100us(ctx);
    }
  return rfail(s,-ETIMEDOUT,"readcheck-poll-bound");
}
static uint32_t rcrc(const uint8_t *p,size_t bytes,bool header)
{
  uint32_t crc=UINT32_MAX;
  for(size_t i=0;i<bytes;i++)
    {
      crc^=header&&i>=16&&i<20?0:p[i];
      for(unsigned j=0;j<8;j++)crc=(crc>>1)^((0-(crc&1))&UINT32_C(0xedb88320));
    }
  return ~crc;
}
static uint64_t rle64(const uint8_t *p)
{return ufs_getle32(p)|(uint64_t)ufs_getle32(p+4)<<32;}

/* Return 1 only for a valid completed CHECK CONDITION with initial reset/
 * media-change UNIT ATTENTION; caller may reissue ONCE before READ(16).
 * All other SCSI errors are surfaced, never interpreted as media data.
 */
static int rresponse(struct a733_ufs_readcheck *s,const uint8_t *r,uint32_t bytes)
{
  unsigned segment=(unsigned)r[10]<<8|r[11],sense=(unsigned)r[32]<<8|r[33];
  s->scsi_status=r[7];
  if(r[0]!=0x21||r[2]!=s->lun||r[3]||r[6]||r[8]||
     (r[1]&~0x60)||(r[9]&~1)||segment>480||sense>18)
    return -EPROTO;
  if(r[9]&1)++s->exceptions; /* Report exception; never clear device flags. */
  if(!r[7])
    return r[1]||segment||sense||ufs_getbe32(r+12)?-EPROTO:0;
  if(r[7]!=2||sense<8||segment<sense+2||ufs_getbe32(r+12)>bytes)return -EREMOTEIO;
  const uint8_t *p=r+34;
  if((p[0]&0x7f)==0x70||(p[0]&0x7f)==0x71)
    {
      if(sense<14||p[7]<6||8u+p[7]>sense)return -EPROTO;
      s->sense_key=p[2]&15;s->asc=p[12];s->ascq=p[13];
    }
  else if((p[0]&0x7f)==0x72||(p[0]&0x7f)==0x73)
    {
      if(8u+p[7]>sense)return -EPROTO;
      s->sense_key=p[1]&15;s->asc=p[2];s->ascq=p[3];
    }
  else return -EPROTO;
  return s->sense_key==6&&((s->asc==0x29&&s->ascq<=7)||
                          (s->asc==0x28&&!s->ascq))?1:-EREMOTEIO;
}

static int rexchange(struct a733_ufs_readcheck *s,const struct a733_ufs_board *b,
                      uint8_t *arena,const struct a733_ufs_nop_io *io,void *ctx,
                      uint8_t opcode,uint64_t lba,uint32_t count)
{
  uint8_t *m=arena+4096,*r=m+A733_UFS_RSP_OFFSET;
  s->opcode=opcode;s->guards=false;
  if(s->reader_active)
    {
      if(!s->reader_budget)return rfail(s,-E2BIG,"reader-command-bound");
      --s->reader_budget;
    }
  else if(s->commands>=10)return rfail(s,-E2BIG,"readcheck-command-bound");
  uint32_t maximum=s->reader_active?A733_UFS_TRANSFER_MAX:A733_UFS_READ_SAMPLE;
  if(opcode!=0x12&&opcode!=0x9e&&opcode!=0x88)
    return rfail(s,-EPERM,"readcheck-opcode-denied");
  if(opcode==0x88&&(!s->identity||ufs_read_range(lba,count,s->blocks)||
                   !count||count>maximum/s->blocksize))
    return rfail(s,-EINVAL,"readcheck-range-denied");
  int ret=rsnapshot(s,b,io,ctx,s->physical,0,1);
  if(ret)return ret;
  int bytes=ufs_read_command(m,s->physical,s->lun,opcode,lba,count,s->blocksize,0);
  if(bytes<=0||(unsigned)bytes>maximum)return rfail(s,-EINVAL,"readcheck-encode-failed");
  memcpy(s->descriptor,m,32);memcpy(s->request,m+A733_UFS_UCD_OFFSET,32);
  memcpy(s->prdt,m+A733_UFS_PRD_OFFSET,16);
  memset(m+A733_UFS_DATA_OFFSET,0xcc,(size_t)bytes);
  io->clean(ctx,(uintptr_t)arena,A733_UFS_NOP_ARENA);
  /* This exact completion bit belongs to our preceding NOP/read. IE stays
   * zero; never acknowledge other bits or clear a doorbell to fake completion.
   */
  if((ret=rwrite(s,io,ctx,0x20,1)))return ret;
  if((ret=rsnapshot(s,b,io,ctx,s->physical,0,0)))return ret;
  s->stopped=false;
  if((ret=rwrite(s,io,ctx,0x60,1))||(ret=rwait(s,io,ctx,0x60,1)))return ret;
  if((ret=rsnapshot(s,b,io,ctx,s->physical,1,0)))return ret;
  ++s->commands;
  if((ret=rwrite(s,io,ctx,0x58,1))||(ret=rwait(s,io,ctx,0x58,0)))return ret;
  if((ret=rsnapshot(s,b,io,ctx,s->physical,1,1)))return ret;
  io->invalidate(ctx,(uintptr_t)arena,A733_UFS_NOP_ARENA);
  s->ocs=ufs_getle32(m+8);
  memcpy(s->response,r,sizeof(s->response));
  for(size_t i=0;i<4096;i++)
    if(arena[i]!=0xa5||m[A733_UFS_DMA_SIZE+i]!=0x5a)
      return rfail(s,-EFAULT,"readcheck-dma-guard-corrupt");
  s->guards=true;
  for(size_t i=0;i<A733_UFS_DMA_SIZE;i++)
    {
      if((i>=A733_UFS_RSP_OFFSET&&i<A733_UFS_PRD_OFFSET)||
         (i>=A733_UFS_DATA_OFFSET&&i<A733_UFS_DATA_OFFSET+(size_t)bytes))continue;
      uint8_t expected=i<32?((i>=8&&i<12)?0:s->descriptor[i]):
        i>=A733_UFS_UCD_OFFSET&&i<A733_UFS_UCD_OFFSET+32?s->request[i-A733_UFS_UCD_OFFSET]:
        i>=A733_UFS_PRD_OFFSET&&i<A733_UFS_PRD_OFFSET+16?s->prdt[i-A733_UFS_PRD_OFFSET]:0;
      if(m[i]!=expected)return rfail(s,-EBADMSG,"readcheck-unexpected-dma-mutation");
    }
  int response=rresponse(s,r,(uint32_t)bytes);
  if(response<0)return rfail(s,response,"readcheck-response-rejected");
  if((ret=rwrite(s,io,ctx,0x60,0))||(ret=rwait(s,io,ctx,0x60,0)))return ret;
  if((ret=rsnapshot(s,b,io,ctx,s->physical,0,1)))return ret;
  s->stopped=true;
  if(!response)s->readbytes+=(uint32_t)bytes;
  return response;
}
static int rcommand(struct a733_ufs_readcheck *s,const struct a733_ufs_board *b,
                     uint8_t *arena,const struct a733_ufs_nop_io *io,void *ctx,
                     uint8_t opcode,uint64_t lba,uint32_t count)
{
  int ret=rexchange(s,b,arena,io,ctx,opcode,lba,count);
  if(ret==1&&opcode!=0x88&&!s->ua_retries)
    {
      ++s->ua_retries;ret=rexchange(s,b,arena,io,ctx,opcode,lba,count);
    }
  return ret==1?rfail(s,-EREMOTEIO,"readcheck-unit-attention-not-reissued"):ret;
}

static void rgpt_header(struct a733_ufs_readcheck *s,const uint8_t *data)
{
  s->protective_mbr=data[510]==0x55&&data[511]==0xaa;
  bool protective=false;
  for(unsigned i=0;i<4;i++)if(data[446+i*16+4]==0xee)protective=true;
  s->protective_mbr=s->protective_mbr&&protective;
  const uint8_t *h=data+s->blocksize;
  s->gpt_error=-ENOENT;
  if(memcmp(h,"EFI PART",8))return;
  s->gpt_present=true;s->gpt_error=-EBADMSG;
  uint32_t length=ufs_getle32(h+12);
  if(ufs_getle32(h+8)!=0x10000||length<92||length>s->blocksize||
     ufs_getle32(h+20)||rcrc(h,length,true)!=ufs_getle32(h+16)||
     rle64(h+24)!=1||rle64(h+32)!=s->blocks-1)return;
  s->first_usable=rle64(h+40);s->last_usable=rle64(h+48);
  s->entry_lba=rle64(h+72);s->entry_count=ufs_getle32(h+80);
  s->entry_size=ufs_getle32(h+84);s->entry_crc=ufs_getle32(h+88);
  if(s->first_usable<2||s->last_usable>=s->blocks-1||
     s->first_usable>s->last_usable||s->entry_lba<2||
     !s->entry_count||s->entry_size<128||s->entry_size>4096||
     (s->entry_size&(s->entry_size-1)))return;
  s->gpt_header_valid=true;s->gpt_error=-E2BIG;
  if(s->entry_count>A733_UFS_READ_SAMPLE/s->entry_size)return;
  s->entry_bytes=s->entry_count*s->entry_size;
  uint64_t blocks=(s->entry_bytes+s->blocksize-1)/s->blocksize;
  if(ufs_read_range(s->entry_lba,blocks,s->blocks)||
     s->entry_lba>s->first_usable||blocks>s->first_usable-s->entry_lba)
    {s->gpt_header_valid=false;s->gpt_error=-EBADMSG;return;}
  s->gpt_error=0;
}
static void rgpt_entries(struct a733_ufs_readcheck *s,const uint8_t *data)
{
  s->gpt_error=-EBADMSG;
  if(rcrc(data,s->entry_bytes,false)!=s->entry_crc)return;
  for(uint32_t i=0;i<s->entry_count;i++)
    {
      const uint8_t *p=data+(size_t)i*s->entry_size;bool used=false;
      for(unsigned j=0;j<16;j++)if(p[j])used=true;
      if(!used)continue;
      uint64_t first=rle64(p+32),last=rle64(p+40);
      if(first<s->first_usable||last>s->last_usable||first>last)return;
      if(s->gpt_partitions<4)
        {s->partitions[s->gpt_partitions][0]=first;s->partitions[s->gpt_partitions][1]=last;}
      ++s->gpt_partitions;
    }
  s->gpt_entries_valid=true;s->gpt_error=0;
}

int a733_ufs_readcheck_run(struct a733_ufs_readcheck *s,
                          const struct a733_ufs_board *b,
                          const struct a733_ufs_nop *nop,uint8_t *arena,uint8_t lun,
                          const struct a733_ufs_nop_io *io,void *ctx)
{
  uintptr_t va=(uintptr_t)arena;uint64_t pa;int ret;
  if(!s||!nop||!arena||!io||!io->read||!io->write||!io->translate||
     !io->clean||!io->invalidate||!io->now_ms||!io->delay_100us||lun>7)return -EINVAL;
  if(s->attempted)return -EALREADY;
  s->attempted=true;s->lun=lun;s->stage="readcheck-preflight";s->gpt_error=-ENOENT;
  if(!nop->completed||!nop->stopped||!nop->guards||nop->error||nop->quarantine)
    return rfail(s,-EACCES,"readcheck-successful-nop-required");
  if((va&4095)||va<UINT64_C(0x40200000)||va>UINT64_C(0x48000000)-A733_UFS_NOP_ARENA||
     nop->physical<UINT64_C(0x40201000)||nop->physical>UINT64_C(0x48000000)-A733_UFS_DMA_SIZE-4096||
     !(va+A733_UFS_NOP_ARENA<=nop->physical-4096||va>=nop->physical+A733_UFS_DMA_SIZE+4096))
    return rfail(s,-EADDRNOTAVAIL,"readcheck-private-arena-invalid");
  if((ret=rsnapshot(s,b,io,ctx,nop->physical,0,1)))return ret;
  for(size_t i=0;i<A733_UFS_NOP_ARENA;i+=4096)
    {
      ret=io->translate(ctx,va+i,&pa);
      if(ret)return rfail(s,ret,"readcheck-cpu-translation-failed");
      if(pa!=va+i)return rfail(s,-EADDRNOTAVAIL,"readcheck-nonidentity-page");
      ++s->pages;
    }
  s->physical=va+4096;memset(arena,0xa5,4096);
  memset(arena+4096,0,A733_UFS_DMA_SIZE);memset(arena+4096+A733_UFS_DMA_SIZE,0x5a,4096);
  io->clean(ctx,va,A733_UFS_NOP_ARENA);
  if((ret=rsnapshot(s,b,io,ctx,nop->physical,0,1)))return ret;
  s->published=true;
  if((ret=rwrite(s,io,ctx,0x50,(uint32_t)s->physical))||
     (ret=rwrite(s,io,ctx,0x54,0)))return ret;
  s->stopped=true;
  uint32_t start=io->now_ms(ctx);
  s->stage="readcheck-inquiry";
  if((ret=rcommand(s,b,arena,io,ctx,0x12,0,0)))return ret;
  const uint8_t *data=arena+4096+A733_UFS_DATA_OFFSET;
  if(data[0]||data[4]<31)return rfail(s,-ENODEV,"readcheck-not-direct-access-lun");
  for(unsigned i=0;i<28;i++)s->model[i]=data[8+i]>=32&&data[8+i]<=126?(char)data[8+i]:'?';
  s->model[28]=0;s->stage="readcheck-capacity16";
  if((ret=rcommand(s,b,arena,io,ctx,0x9e,0,0)))return ret;
  if(ufs_read_capacity(data,&s->blocks,&s->blocksize))return rfail(s,-EOVERFLOW,"readcheck-capacity-rejected");
  uint32_t count=A733_UFS_READ_SAMPLE/s->blocksize;
  if(s->blocks<3u*count+2)return rfail(s,-EOVERFLOW,"readcheck-media-too-small");
  s->identity=true;s->lbas[0]=0;s->lbas[1]=(s->blocks/2/count)*count;s->lbas[2]=s->blocks-count;
  for(unsigned i=0;i<3;i++)
    {
      s->stage="readcheck-sample";
      if((ret=rcommand(s,b,arena,io,ctx,0x88,s->lbas[i],count)))return ret;
      memcpy(s->reference,data,A733_UFS_READ_SAMPLE);s->crc[i]=rcrc(data,A733_UFS_READ_SAMPLE,false);
      s->stage="readcheck-repeat";
      if((ret=rcommand(s,b,arena,io,ctx,0x88,s->lbas[i],count)))return ret;
      s->repeat_crc[i]=rcrc(data,A733_UFS_READ_SAMPLE,false);
      if(memcmp(s->reference,data,A733_UFS_READ_SAMPLE))return rfail(s,-EBADMSG,"readcheck-repeat-mismatch");
      ++s->samples;
      if(i==0)
        {
          rgpt_header(s,data);
          if(s->gpt_header_valid&&!s->gpt_error)
            {
              s->stage="readcheck-gpt-entries";
              if((ret=rcommand(s,b,arena,io,ctx,0x88,s->entry_lba,
                               (s->entry_bytes+s->blocksize-1)/s->blocksize)))return ret;
              rgpt_entries(s,data);
            }
        }
    }
  s->elapsed_ms=io->now_ms(ctx)-start;s->completed=true;
  s->stage="readcheck-passed-not-mounted";return 0;
}

int a733_ufs_reader_ready(struct a733_ufs_readcheck *s,
                         const struct a733_ufs_board *b,uint8_t *arena,
                         const struct a733_ufs_nop_io *io,void *ctx)
{
  if(!s||!b||!arena||!io||!io->read||!io->write||!io->translate||
     !io->clean||!io->invalidate||!io->now_ms||!io->delay_100us)return -EINVAL;
  if(s->reader_active)return -EBUSY;
  if(!s->completed||!s->identity||s->samples!=3||s->error||s->quarantine||
     !s->stopped||!s->guards||s->pages!=19)return -EACCES;
  uintptr_t va=(uintptr_t)arena;
  if((va&4095)||va<UINT64_C(0x40200000)||
     va>UINT64_C(0x48000000)-A733_UFS_NOP_ARENA||s->physical!=va+4096)
    return rfail(s,-EFAULT,"reader-arena-identity-lost");
  for(size_t i=0;i<A733_UFS_NOP_ARENA;i+=4096)
    {
      uint64_t pa;int ret=io->translate(ctx,va+i,&pa);
      if(ret)return rfail(s,ret,"reader-page-translation-failed");
      if(pa!=va+i)return rfail(s,-EFAULT,"reader-page-identity-lost");
    }
  for(size_t i=0;i<4096;i++)
    if(arena[i]!=0xa5||arena[4096+A733_UFS_DMA_SIZE+i]!=0x5a)
      return rfail(s,-EFAULT,"reader-existing-guard-corrupt");
  return rsnapshot(s,b,io,ctx,s->physical,0,1);
}

int a733_ufs_reader_read(struct a733_ufs_readcheck *s,
                        const struct a733_ufs_board *b,uint8_t *arena,
                        uint8_t *buffer,uint64_t lba,uint32_t count,
                        const struct a733_ufs_nop_io *io,void *ctx)
{
  if(!s||(s->blocksize!=512&&s->blocksize!=4096)||
     ufs_read_range(lba,count,s->blocks))return -EINVAL;
  if(!count)return 0;
  if(!buffer)return -EINVAL;
  if(count>1048576u/s->blocksize)return -E2BIG;
  size_t bytes=(size_t)count*s->blocksize;uintptr_t dest=(uintptr_t)buffer;
  uintptr_t va=(uintptr_t)arena,st=(uintptr_t)s;
  if(dest>UINTPTR_MAX-bytes||
     !(dest+bytes<=va||dest>=va+A733_UFS_NOP_ARENA)||
     !(dest+bytes<=st||dest>=st+sizeof(*s)))return -EINVAL;
  if(s->commands>UINT32_MAX-64||s->writes>UINT32_MAX-256||
     s->polls>UINT32_MAX-2000000||s->reader_calls==UINT64_MAX||
     s->readbytes>UINT64_MAX-bytes||s->reader_bytes>UINT64_MAX-bytes)
    return -EOVERFLOW;
  int ret=a733_ufs_reader_ready(s,b,arena,io,ctx);
  if(ret)return ret;
  uint32_t start=io->now_ms(ctx),done=0;
  ++s->reader_calls;s->reader_active=true;s->reader_budget=64;
  while(done<count)
    {
      uint32_t chunk=A733_UFS_TRANSFER_MAX/s->blocksize;
      if(chunk>count-done)chunk=count-done;
      ret=rcommand(s,b,arena,io,ctx,0x88,lba+done,chunk);
      if(ret)break;
      size_t amount=(size_t)chunk*s->blocksize;
      memcpy(buffer+(size_t)done*s->blocksize,
             arena+4096+A733_UFS_DATA_OFFSET,amount);
      s->reader_bytes+=amount;done+=chunk;
    }
  s->reader_active=false;s->reader_budget=0;
  s->reader_elapsed_ms=io->now_ms(ctx)-start;
  if(!ret)s->stage="reader-read-completed-not-mounted";
  return ret;
}

size_t a733_ufs_readcheck_report(const struct a733_ufs_readcheck *s,char *out,size_t cap)
{
  if(!s||!out||!cap)return 0;
  int n=snprintf(out,cap,
    "readcheck-v170: attempted=%u completed=%u stopped=%u guards=%u quarantine=%u last=%d stage=%s\n"
    "readcheck-dma: physical=%016" PRIx64 " published=%u pages=%" PRIu32 " writes=%" PRIu32 " polls=%" PRIu32 "\n"
    "readcheck-media: lun=%u model='%s' blocks=%" PRIu64 " block-bytes=%" PRIu32 " capacity-bytes=%" PRIu64 " identity=%u\n"
    "readcheck-io: commands=%" PRIu32 " transferred=%" PRIu64 " samples=%" PRIu32 " ua-reissues=%" PRIu32 " elapsed-ms=%" PRIu32 " exceptions=%" PRIu32 "\n"
    "readcheck-sample[0]: lba=%" PRIu64 " bytes=16384 crc=%08" PRIx32 "/%08" PRIx32 "\n"
    "readcheck-sample[1]: lba=%" PRIu64 " bytes=16384 crc=%08" PRIx32 "/%08" PRIx32 "\n"
    "readcheck-sample[2]: lba=%" PRIu64 " bytes=16384 crc=%08" PRIx32 "/%08" PRIx32 "\n"
    "readcheck-gpt: protective-mbr=%u present=%u header-valid=%u entries-valid=%u status=%d partitions=%" PRIu32 "\n"
    "readcheck-partitions[0..3]: %" PRIu64 "..%" PRIu64 " / %" PRIu64 "..%" PRIu64 " / %" PRIu64 "..%" PRIu64 " / %" PRIu64 "..%" PRIu64 "\n"
    "readcheck-sense: opcode=%02x status=%02x key=%02x asc=%02x ascq=%02x\n"
    "readcheck-wire: ocs=%08" PRIx32 " upiu=%02x %02x %02x %02x %02x %02x %02x %02x segment=%u sense=%u residual=%" PRIu32 "\n"
    "readcheck-policy: one qualification batch; own completion W1C bit0 only; no reset/timeout retry/free/mount/media-write\n"
    "reader-v171: calls=%" PRIu64 " bytes=%" PRIu64 " elapsed-ms=%" PRIu32 " active=%u chunk-max=65536 request-max=1048576\n",
    s->attempted,s->completed,s->stopped,s->guards,s->quarantine,s->error,s->stage?s->stage:"not-attempted",
    s->physical,s->published,s->pages,s->writes,s->polls,s->lun,s->model,s->blocks,s->blocksize,s->blocks*s->blocksize,s->identity,
    s->commands,s->readbytes,s->samples,s->ua_retries,s->elapsed_ms,s->exceptions,
    s->lbas[0],s->crc[0],s->repeat_crc[0],s->lbas[1],s->crc[1],s->repeat_crc[1],s->lbas[2],s->crc[2],s->repeat_crc[2],
    s->protective_mbr,s->gpt_present,s->gpt_header_valid,s->gpt_entries_valid,s->gpt_error,s->gpt_partitions,
    s->partitions[0][0],s->partitions[0][1],s->partitions[1][0],s->partitions[1][1],
    s->partitions[2][0],s->partitions[2][1],s->partitions[3][0],s->partitions[3][1],
    s->opcode,s->scsi_status,s->sense_key,s->asc,s->ascq,s->ocs,
    s->response[0],s->response[1],s->response[2],s->response[3],
    s->response[4],s->response[5],s->response[6],s->response[7],
    (unsigned)s->response[10]<<8|s->response[11],
    (unsigned)s->response[32]<<8|s->response[33],ufs_getbe32(s->response+12),
    s->reader_calls,s->reader_bytes,s->reader_elapsed_ms,s->reader_active);
  if(n<0){out[0]=0;return 0;}return (size_t)n<cap?(size_t)n:cap-1;
}
