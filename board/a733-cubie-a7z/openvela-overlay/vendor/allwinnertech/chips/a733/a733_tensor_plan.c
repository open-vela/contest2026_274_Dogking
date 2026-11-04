/* SPDX-License-Identifier: Apache-2.0 */
#include "a733_tensor_plan.h"
#include <errno.h>
#include <string.h>

_Static_assert(sizeof(struct a733_tensor_header_s) == 128, "A7TV header ABI");
_Static_assert(sizeof(struct a733_tensor_region_s) == 32, "A7TV region ABI");
_Static_assert(sizeof(struct a733_tensor_desc_s) == 64, "A7TV tensor ABI");
_Static_assert(sizeof(struct a733_tensor_ref_s) == 16, "tensor ref ABI");

static int zeros(const uint32_t *p, size_t n)
{
  while (n--) if (*p++) return 0;
  return 1;
}
static uint32_t rounded(uint32_t n)
{
  return (n + A733_TENSOR_PAGE - 1u) & ~(A733_TENSOR_PAGE - 1u);
}
static int overlaps(uint32_t a, uint32_t na, uint32_t b, uint32_t nb)
{
  return (uint64_t)a < (uint64_t)b + nb && (uint64_t)b < (uint64_t)a + na;
}
int a733_tensor_header_check(const struct a733_tensor_header_s *h,
                             uint64_t file_size, uint32_t *bytes)
{
  if (!h || !bytes || h->magic != A733_TENSOR_MAGIC ||
      h->version != A733_TENSOR_FILE_VERSION || h->header_size != sizeof(*h) ||
      h->cid != A733_TENSOR_CID || h->flags || !zeros(h->reserved,12) ||
      !memchr(h->name,0,sizeof(h->name)) || !h->regions || h->regions > A733_TENSOR_MAX_REGIONS ||
      !h->inputs || h->inputs > A733_TENSOR_MAX_IO || !h->outputs || h->outputs > A733_TENSOR_MAX_IO ||
      h->command_region >= h->regions || !file_size || file_size > A733_TENSOR_FILE_LIMIT ||
      file_size != h->total_size) return -EPROTO;
  *bytes = (uint32_t)sizeof(*h) + h->regions * 32u + (h->inputs+h->outputs) * 64u;
  return *bytes > h->total_size ? -EFBIG : 0;
}
static int tensor_check(const struct a733_tensor_plan_s *p,
                         const struct a733_tensor_desc_s *d, int output)
{
  const struct a733_tensor_region_s *r;
  uint64_t elements=1;
  uint32_t i, width;
  if (d->region >= p->header.regions || !d->bytes || d->quant ||
      !d->rank || d->rank > 4 || !zeros(d->reserved,5)) return -EPROTO;
  r=&p->region[d->region];
  if (d->offset > r->bytes || d->bytes > r->bytes-d->offset ||
      (r->flags & A733_TENSOR_COMMAND) ||
      !(r->flags & (output ? A733_TENSOR_OUTPUT : A733_TENSOR_LOAD))) return -EPROTO;
  switch (d->format) {
    case 0: case 8: case 9: width=4; break;
    case 1: case 4: case 5: width=2; break;
    case 2: case 3: case 16: case A733_TENSOR_OPAQUE: width=1; break;
    case 10: case 11: case 12: width=8; break;
    default: return -ENOTSUP;
  }
  if (d->bytes % width || d->offset % width) return -EPROTO;
  for(i=0;i<4;++i) {
    if(i<d->rank) {
      if(!d->dims[i] || elements > d->bytes/width/d->dims[i]) return -EOVERFLOW;
      elements*=d->dims[i];
    } else if(d->dims[i]) return -EPROTO;
  }
  if(d->format==A733_TENSOR_OPAQUE && elements != d->bytes) return -EPROTO;
  return 0;
}
int a733_tensor_plan_check(struct a733_tensor_plan_s *p, uint64_t file_size,
                           a733_tensor_crc_t crc)
{
  struct a733_tensor_header_s checked;
  uint32_t i,j,cursor=0,sum;
  int ret;
  if(!p || !crc) return -EINVAL;
  ret=a733_tensor_header_check(&p->header,file_size,&p->descriptor_bytes);
  if(ret<0) return ret;
  checked=p->header; checked.manifest_crc32=0;
  sum=crc((const uint8_t *)&checked,sizeof(checked),0);
  sum=crc((const uint8_t *)p->region,p->header.regions*32u,sum);
  sum=crc((const uint8_t *)p->input,p->header.inputs*64u,sum);
  sum=crc((const uint8_t *)p->output,p->header.outputs*64u,sum);
  if(sum != p->header.manifest_crc32) return -EBADMSG;
  for(i=0;i<p->header.regions;++i) {
    const struct a733_tensor_region_s *r=&p->region[i];
    if(!r->bytes || r->bytes>A733_TENSOR_POOL || (r->vip & 4095u) ||
       r->vip < A733_TENSOR_PAGE || (r->flags & ~7u) || !(r->flags & 3u) ||
       !zeros(r->reserved,3) || (uint64_t)r->vip+rounded(r->bytes)>UINT32_MAX) return -EPROTO;
    if((r->flags & A733_TENSOR_COMMAND) && (r->flags != (A733_TENSOR_COMMAND|A733_TENSOR_LOAD))) return -EPROTO;
    if(r->flags & A733_TENSOR_LOAD) {
      if(r->file_offset<p->descriptor_bytes || r->file_offset>p->header.total_size ||
         r->bytes>p->header.total_size-r->file_offset) return -EFBIG;
    } else if(r->file_offset || r->crc32) return -EPROTO;
    for(j=0;j<i;++j) {
      const struct a733_tensor_region_s *other=&p->region[j];
      if(overlaps(r->vip,rounded(r->bytes),other->vip,rounded(other->bytes))) return -EADDRINUSE;
      if((r->flags & other->flags & A733_TENSOR_LOAD) &&
         overlaps(r->file_offset,r->bytes,other->file_offset,other->bytes)) return -EADDRINUSE;
    }
    if(rounded(r->bytes)>A733_TENSOR_POOL-cursor) return -ENOMEM;
    p->pool_offset[i]=cursor; cursor+=rounded(r->bytes);
  }
  {
    const struct a733_tensor_region_s *command=&p->region[p->header.command_region];
    if(command->flags != (A733_TENSOR_LOAD|A733_TENSOR_COMMAND) ||
       (p->header.command_offset & 7u) || (command->bytes & 7u) ||
       p->header.command_offset>=command->bytes ||
       command->bytes-p->header.command_offset<8u) return -EPROTO;
    p->tail_vip=command->vip+rounded(command->bytes);
    if(p->tail_vip>UINT32_MAX-A733_TENSOR_PAGE) return -EOVERFLOW;
    for(i=0;i<p->header.regions;++i)
      if(overlaps(p->tail_vip,A733_TENSOR_PAGE,p->region[i].vip,rounded(p->region[i].bytes))) return -EADDRINUSE;
  }
  if(A733_TENSOR_PAGE>A733_TENSOR_POOL-cursor) return -ENOMEM;
  p->tail_pool_offset=cursor; p->pool_bytes=cursor+A733_TENSOR_PAGE;
  for(i=0;i<p->header.inputs+p->header.outputs;++i) {
    const struct a733_tensor_desc_s *d=i<p->header.inputs ? &p->input[i] : &p->output[i-p->header.inputs];
    ret=tensor_check(p,d,i>=p->header.inputs); if(ret<0) return ret;
    for(j=0;j<i;++j) {
      const struct a733_tensor_desc_s *other=j<p->header.inputs ? &p->input[j] : &p->output[j-p->header.inputs];
      if(d->region==other->region && overlaps(d->offset,d->bytes,other->offset,other->bytes)) return -EADDRINUSE;
    }
  }
  return 0;
}
int a733_tensor_input_check(const struct a733_tensor_plan_s *p,
                            const struct a733_tensor_run_s *run,
                            const void *const *input, a733_tensor_crc_t crc)
{
  uint32_t i;
  if(!p || !run || !input || !crc || run->version!=A733_TENSOR_ABI ||
     run->inputs!=p->header.inputs || run->outputs!=p->header.outputs ||
     !run->timeout_ms || run->timeout_ms>5000 ||
     (run->flags & ~(A733_TENSOR_VERIFY_GOLDEN|A733_TENSOR_RESIDENT|A733_TENSOR_FULL_CLEAN|A733_TENSOR_CRC_LEGACY))) return -EINVAL;
  for(i=0;i<run->inputs;++i) {
    if(!input[i] || run->input[i].bytes!=p->input[i].bytes || run->input[i].reserved) return -EINVAL;
    if((run->flags & A733_TENSOR_VERIFY_GOLDEN) &&
       crc(input[i],p->input[i].bytes,0)!=p->input[i].crc32) return -EBADMSG;
  }
  for(i=0;i<run->outputs;++i)
    if(run->output[i].bytes!=p->output[i].bytes || run->output[i].reserved) return -EINVAL;
  return 0;
}
void a733_tensor_apply(const struct a733_tensor_plan_s *p, void *const *regions,
                       const void *const *input)
{
  uint32_t i;
  for(i=0;i<p->header.inputs;++i)
    memcpy((uint8_t *)regions[p->input[i].region]+p->input[i].offset,input[i],p->input[i].bytes);
}
void a733_tensor_prefill(const struct a733_tensor_plan_s *p, void *const *regions)
{
  uint32_t i;
  for(i=0;i<p->header.outputs;++i)
    memset((uint8_t *)regions[p->output[i].region]+p->output[i].offset,0xa5,p->output[i].bytes);
}
int a733_tensor_output_check(const struct a733_tensor_plan_s *p,
                             struct a733_tensor_run_s *run,
                             void *const *regions, a733_tensor_crc_t crc)
{
  uint32_t i,j;
  int ret=0;
  for(i=0;i<p->header.outputs;++i) {
    const uint8_t *out=(uint8_t *)regions[p->output[i].region]+p->output[i].offset;
    run->output_crc32[i]=crc(out,p->output[i].bytes,0); run->output_changed[i]=0;
    for(j=0;j<p->output[i].bytes;++j) run->output_changed[i]+=out[j]!=0xa5;
    if(!run->output_changed[i] || ((run->flags & A733_TENSOR_VERIFY_GOLDEN) &&
       run->output_crc32[i]!=p->output[i].crc32)) ret=-EILSEQ;
  }
  return ret;
}
void a733_tensor_commit(const struct a733_tensor_plan_s *p, void *const *regions,
                        void *const *output)
{
  uint32_t i;
  for(i=0;i<p->header.outputs;++i)
    memcpy(output[i],(uint8_t *)regions[p->output[i].region]+p->output[i].offset,p->output[i].bytes);
}
