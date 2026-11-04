/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include "a733_ufs_nop.h"

static int nfail(struct a733_ufs_nop *s, int error, const char *stage)
{
  s->error = error < 0 ? error : -EPROTO;
  s->stage = stage;
  s->quarantine = s->writes != 0;
  return s->error;
}

static int nsnapshot(struct a733_ufs_nop *s, const struct a733_ufs_board *b,
                     const struct a733_ufs_nop_io *io, void *ctx,
                     uint64_t base, uint32_t run, uint32_t status)
{
  int ret = a733_ufs_handoff_snapshot(&s->after, b, io->read, ctx);
  if (ret) return nfail(s, ret, "nop-snapshot-failed");
  if (!s->after.stable || !s->after.link_ready || s->after.pending ||
      s->after.raw[0] != 0x11870f1f || s->after.raw[1] != 0x300 ||
      s->after.raw[2] || s->after.raw[3] != status || s->after.raw[4] ||
      s->after.raw[5] != 0x10f || s->after.raw[6] != 1 || s->after.raw[7] ||
      s->after.raw[8] != (uint32_t)base || s->after.raw[9] != base >> 32 ||
      s->after.raw[11] != run || s->after.raw[12] || s->after.raw[13] ||
      s->after.raw[15]) return nfail(s, -EBUSY, "nop-unexpected-hci-state");
  return 0;
}

static int nwrite(struct a733_ufs_nop *s, const struct a733_ufs_nop_io *io,
                  void *ctx, uint32_t offset, uint32_t value)
{
  int ret;
  s->last_offset = offset;
  ++s->writes; /* A failed store may have reached hardware. */
  ret = io->write(ctx, offset, value);
  return ret ? nfail(s, ret, "nop-write-failed") : 0;
}

static int nwait(struct a733_ufs_nop *s, const struct a733_ufs_nop_io *io,
                 void *ctx, uint32_t offset, uint32_t mask, uint32_t expected,
                 unsigned int limit, uint32_t timeout)
{
  uint32_t value;
  uint32_t is;
  uint32_t start = io->now_ms(ctx);
  unsigned int i;
  int ret;
  s->last_offset = offset;
  for (i = 0; i < limit; i++)
    {
      ++s->polls;
      ret = io->read(ctx, 0x20, &is);
      if (ret) return nfail(s, ret, "nop-is-read-failed");
      if (is & ~UINT32_C(1)) return nfail(s, -EIO, "nop-controller-event");
      ret = io->read(ctx, offset, &value);
      if (ret) return nfail(s, ret, "nop-poll-read-failed");
      if (offset==0x58 && (value & ~UINT32_C(1)))
        return nfail(s,-EBUSY,"nop-unowned-doorbell");
      s->wait_ms = io->now_ms(ctx) - start;
      if (s->wait_ms > timeout) return nfail(s, -ETIMEDOUT, "nop-deadline");
      if ((value & mask) == expected) return 0;
      io->delay_100us(ctx);
    }
  return nfail(s, -ETIMEDOUT, "nop-poll-bound");
}

int a733_ufs_nop_run(struct a733_ufs_nop *s,
                    const struct a733_ufs_board *b,
                    const struct a733_ufs_quiesce *q, uint8_t *arena,
                    const struct a733_ufs_nop_io *io, void *ctx)
{
  uint64_t translated;
  uintptr_t va = (uintptr_t)arena;
  uint8_t *m;
  size_t i;
  uint8_t expected;
  int ret;
  if (!s || !q || !arena || !io || !io->read || !io->write || !io->translate ||
      !io->clean || !io->invalidate || !io->now_ms || !io->delay_100us)
    return -EINVAL;
  if (s->attempted) return -EALREADY;
  s->attempted = true;
  s->stage = "nop-preflight";
  if (!q->completed || q->error || q->quarantine || !q->after.stable ||
      q->after.pending || q->after.raw[4] || q->after.raw[11] || q->after.raw[15])
    return nfail(s, -EACCES, "nop-quiesce-required");
  if ((va & 4095) || va < UINT64_C(0x40200000) ||
      va > UINT64_C(0x48000000) - A733_UFS_NOP_ARENA)
    return nfail(s, -EADDRNOTAVAIL, "nop-arena-outside-private-stage1");
  ret = nsnapshot(s,b,io,ctx,
                  (uint64_t)q->after.raw[8] | (uint64_t)q->after.raw[9]<<32,0,0);
  if (ret) return ret;
  s->before = s->after;
  for (i = 0; i < A733_UFS_NOP_ARENA; i += 4096)
    {
      ret = io->translate(ctx,va+i,&translated);
      if (ret) return nfail(s,ret,"nop-cpu-translation-failed");
      if (translated != (uint64_t)va+i)
        return nfail(s,-EADDRNOTAVAIL,"nop-nonidentity-page");
      ++s->pages;
    }
  m = arena + A733_UFS_NOP_GUARD;
  s->physical = (uint64_t)va + A733_UFS_NOP_GUARD;
  memset(arena,0xa5,A733_UFS_NOP_GUARD);
  memset(m+A733_UFS_DMA_SIZE,0x5a,A733_UFS_NOP_GUARD);
  ret = ufs_read_command(m,s->physical,0,0,0,0,0,0);
  if (ret) return nfail(s,ret,"nop-encode-failed");
  memcpy(s->descriptor,m,32);
  memcpy(s->request,m+A733_UFS_UCD_OFFSET,32);
  io->clean(ctx,va,A733_UFS_NOP_ARENA);
  ret = nsnapshot(s,b,io,ctx,
                  (uint64_t)s->before.raw[8] | (uint64_t)s->before.raw[9]<<32,0,0);
  if (ret) return ret;
  s->stage = "nop-publishing-private-base";
  s->published = true; /* Retain even if the first base write returns error. */
  if ((ret=nwrite(s,io,ctx,0x50,(uint32_t)s->physical)) ||
      (ret=nwrite(s,io,ctx,0x54,0))) return ret;
  ret = nsnapshot(s,b,io,ctx,s->physical,0,0);
  if (ret) return ret;
  if ((ret=nwrite(s,io,ctx,0x60,1))) return ret;
  ret = nwait(s,io,ctx,0x60,UINT32_MAX,1,200,20);
  if (ret) return ret;
  ret = nsnapshot(s,b,io,ctx,s->physical,1,0);
  if (ret) return ret;
  s->stage = "nop-one-request";
  s->submitted = true;
  if ((ret=nwrite(s,io,ctx,0x58,1))) return ret;
  ret = nwait(s,io,ctx,0x58,UINT32_MAX,0,10000,1000);
  if (ret) return ret;
  ret = nsnapshot(s,b,io,ctx,s->physical,1,1);
  if (ret) return ret;
  io->invalidate(ctx,va,A733_UFS_NOP_ARENA);
  memcpy(s->response,m+A733_UFS_RSP_OFFSET,32);
  for (i=0;i<A733_UFS_NOP_GUARD;i++)
    if (arena[i]!=0xa5 || m[A733_UFS_DMA_SIZE+i]!=0x5a)
      return nfail(s,-EFAULT,"nop-dma-guard-corrupt");
  s->guards = true;
  for (i=0;i<A733_UFS_DMA_SIZE;i++)
    {
      if (i>=A733_UFS_RSP_OFFSET && i<A733_UFS_RSP_OFFSET+32) continue;
      expected = i<32 ? ((i>=8 && i<12) ? 0 : s->descriptor[i]) :
                 i>=A733_UFS_UCD_OFFSET && i<A733_UFS_UCD_OFFSET+32 ?
                 s->request[i-A733_UFS_UCD_OFFSET] : 0;
      if (m[i]!=expected) return nfail(s,-EBADMSG,"nop-unexpected-dma-mutation");
    }
  if (ufs_read_response(m,0,0,0) || s->response[8] ||
      s->response[10] || s->response[11])
    return nfail(s,-EIO,"nop-response-rejected");
  s->stage = "nop-stopping-private-queue";
  if ((ret=nwrite(s,io,ctx,0x60,0))) return ret;
  ret = nwait(s,io,ctx,0x60,UINT32_MAX,0,200,20);
  if (ret) return ret;
  ret = nsnapshot(s,b,io,ctx,s->physical,0,1);
  if (ret) return ret;
  s->completed = true;
  s->stopped = true;
  s->stage = "nop-dma-passed-not-media-qualified";
  return 0;
}

size_t a733_ufs_nop_report(const struct a733_ufs_nop *s, char *out, size_t cap)
{
  int n;
  if (!s || !out || !cap) return 0;
  n = snprintf(out, cap,
    "dma-nop-v169: attempted=%u completed=%u stopped=%u quarantine=%u last=%d"
    " stage=%s pages=%" PRIu32 " writes=%" PRIu32 " polls=%" PRIu32 "\n"
    "nop-dma: physical=%016" PRIx64 " published=%u submitted=%u guards=%u"
    " last-offset=%02" PRIx32 " wait-ms=%" PRIu32 " retained-until-reboot=1\n"
    "nop-response: %02x %02x %02x %02x %02x %02x %02x %02x\n"
    "nop-policy: one NOP only; no SCSI/media/block/format; no IS/doorbell clear;"
    " no reset/retry/free; DMA routing is validated only by actual response\n",
    s->attempted,s->completed,s->stopped,s->quarantine,s->error,
    s->stage ? s->stage : "not-attempted",s->pages,s->writes,s->polls,
    s->physical,s->published,s->submitted,s->guards,s->last_offset,s->wait_ms,
    s->response[0],s->response[1],s->response[2],s->response[3],
    s->response[4],s->response[5],s->response[6],s->response[7]);
  if (n < 0) {out[0] = 0; return 0;}
  return (size_t)n < cap ? (size_t)n : cap - 1;
}
