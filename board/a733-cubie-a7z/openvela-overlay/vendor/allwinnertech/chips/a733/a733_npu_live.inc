/* SPDX-License-Identifier: Apache-2.0 */
/* Private to runtime.c. All calls except CANCEL are under tensor_gate.
 * The CPU snapshot is never submitted to DMA. Device memory remains in the
 * original permanent prepared pool; each RUN still restores prior MMU roots.
 * Only CRC-proven unchanged LOAD-only regions may be retained across RUNs.
 * An exclusive logical lease is held between runs: all admitted mutating
 * ioctls, writes and closes invalidate it. Every hardware run rechecks all
 * retained LOADs BEFORE output publication; no pre-run duplicate CRC pass.
 * Commands/inputs and any hardware-mutated LOAD region are refreshed. */
struct a733_rt_live_s
{
  uint8_t *snapshot;
  size_t bytes;
  uint32_t crc;
  char path[A733_NPU_MODEL_PATH_MAX];
  bool loaded,warm,eligible[A733_RT_A7PM_MAX_REGIONS],retained[A733_RT_A7PM_MAX_REGIONS];
  /* CPU snapshot qualification, not a lease on device-written memory. */
  bool input_template_ok[A733_RT_A7PM_MAX_REGIONS],input_skipped[A733_RT_A7PM_MAX_REGIONS];
  uint32_t input_offset[A733_RT_A7PM_MAX_REGIONS],input_size[A733_RT_A7PM_MAX_REGIONS];
  uint32_t input_crc[A733_RT_A7PM_MAX_REGIONS];
  struct a733_npu_live_profile_s profile;
};
static struct a733_rt_live_s *g_rt_live,*g_rt_live_owner;
static int a733_rt_run_a7pm(struct a733_npu_run_s *);
static uint64_t a733_rt_live_now(void)
{
  struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
  return (uint64_t)t.tv_sec*1000000u+t.tv_nsec/1000u;
}
static void a733_rt_live_release(struct a733_rt_context_s *c)
{
  if(g_rt_live_owner==c->live)g_rt_live_owner=NULL;
  if(c->live){kmm_free(c->live->snapshot);kmm_free(c->live);c->live=NULL;}
}
static int a733_rt_live_import(struct a733_rt_context_s *c,
                              struct a733_npu_live_prepare_s *p)
{
  if(p->version!=A733_NPU_LIVE_ABI)return -EINVAL;
  p->status=-EAGAIN;p->bytes=p->crc32=0;
  int r=a733_rt_validate_path(p->path);if(r)return p->status=r;
  if(strcmp(p->path,"/camram-v204/model.a7pm"))return p->status=-EACCES;
  if(c->live)return p->status=-EALREADY;
  uint64_t start=a733_rt_live_now();struct file f;
  r=file_open(&f,p->path,O_RDONLY|O_CLOEXEC);if(r)return p->status=r;
  off_t bytes=file_seek(&f,0,SEEK_END);
  struct a733_rt_live_s *s=NULL;
  if(bytes!=8017024)r=-EMSGSIZE;
  if(!r){s=kmm_zalloc(sizeof(*s));if(!s)r=-ENOMEM;}
  if(!r){s->snapshot=kmm_malloc((size_t)bytes);if(!s->snapshot)r=-ENOMEM;}
  if(!r)r=a733_rt_file_read_exact(&f,0,s->snapshot,(size_t)bytes,&s->crc);
  int closed=file_close(&f);if(!r && closed)r=closed;
  if(!r)
    {
      struct a733_rt_a7pm_header_s h;memcpy(&h,s->snapshot,sizeof(h));
      if(h.magic!=A733_RT_A7PM_MAGIC || h.version!=3 || h.header_size!=sizeof(h) ||
         h.cid!=A733_RT_A7PM_CID || h.total_size!=(uint32_t)bytes ||
         memcmp(h.name,"yolov8n_6_pcq_a",16) || s->crc!=UINT32_C(0xf5f558c5))r=-EBADMSG;
    }
  if(r){if(s){kmm_free(s->snapshot);kmm_free(s);}return p->status=r;}
  s->bytes=(size_t)bytes;strlcpy(s->path,p->path,sizeof(s->path));
  s->profile.version=A733_NPU_LIVE_ABI;s->profile.import_us=a733_rt_live_now()-start;
  s->profile.model_bytes=p->bytes=(uint32_t)s->bytes;s->profile.model_crc32=p->crc32=s->crc;
  c->live=s;return p->status=0;
}
static int a733_rt_live_read(struct file *f,off_t offset,void *dst,size_t n,uint32_t *crc)
{
  if(!g_rt_live)return a733_rt_file_read_exact(f,offset,dst,n,crc);
  if(offset<0 || (uint64_t)offset>g_rt_live->bytes || n>g_rt_live->bytes-(size_t)offset)return -EFBIG;
  memcpy(dst,g_rt_live->snapshot+(size_t)offset,n);
  if(crc)*crc=crc32part(dst,n,*crc);
  return 0;
}
static int a733_rt_live_load(struct file *f,unsigned i,
  const struct a733_rt_a7pm_region_s *region,struct a733_npu_buffer_s *mapped,
  bool input,uint32_t *checksum)
{
  if(g_rt_live)
    {if(i>=A733_RT_A7PM_MAX_REGIONS)return -EINVAL;g_rt_live->retained[i]=false;}
  if(g_rt_live && g_rt_live->warm && i<A733_RT_A7PM_MAX_REGIONS &&
     g_rt_live->eligible[i] && !input && !(region->flags&(A733_RT_A7PM_COMMAND|A733_RT_A7PM_OUTPUT)))
    {
      /* Only the SAME current pool owner can consume its prior verified
       * lease. Data is rechecked after hardware, before any result escapes.
       * No clean/invalidate/copy is needed for these CPU-unwritten pages. */
      if(g_rt_live_owner!=g_rt_live)return -ESTALE;
      *checksum=region->crc32;
      g_rt_live->retained[i]=true;++g_rt_live->profile.retained_regions;return 0;
    }
  return a733_rt_live_read(f,region->file_offset,mapped->cpu,region->size,checksum);
}
/* Return 1 only for an exactly overwritten, padding-free INPUT region.
 * The CPU-only immutable template is CRC-qualified once per descriptor.
 * Short inputs, command overlap, non-live or non-dynamic calls keep legacy
 * loading. The subsequent exact file loader MUST succeed before submission. */
static int a733_rt_live_skip_input(unsigned i,
  const struct a733_rt_a7pm_region_s *r,uint32_t bytes,uint32_t crc,bool dynamic)
{
  if(!g_rt_live || !dynamic || i>=A733_RT_A7PM_MAX_REGIONS ||
     r->flags!=A733_RT_A7PM_LOAD || !bytes || bytes!=r->size)return 0;
  struct a733_rt_live_s *s=g_rt_live;
  if(r->file_offset>s->bytes || r->size>s->bytes-r->file_offset)return -EFBIG;
  if(!s->input_template_ok[i] || s->input_offset[i]!=r->file_offset ||
     s->input_size[i]!=r->size || s->input_crc[i]!=crc)
    {
      uint32_t actual=crc32part(s->snapshot+r->file_offset,r->size,0);
      if(actual!=r->crc32 || actual!=crc)return -EBADMSG;
      s->input_template_ok[i]=true;s->input_offset[i]=r->file_offset;
      s->input_size[i]=r->size;s->input_crc[i]=crc;
    }
  else if(r->crc32!=crc)return -EBADMSG;
  s->input_skipped[i]=true;return 1;
}
static bool a733_rt_live_clean_required(unsigned i,const struct a733_rt_a7pm_region_s *region)
{
  /* Post-run invalidate+CRC only reads retained LOAD data; neither CPU nor another
   * admitted client can dirty these private pages between checks. Every
   * written command/input/output/scratch region still requires its clean. */
  return !g_rt_live || i>=A733_RT_A7PM_MAX_REGIONS || !g_rt_live->retained[i] ||
    !(region->flags&A733_RT_A7PM_LOAD) || (region->flags&(A733_RT_A7PM_COMMAND|A733_RT_A7PM_OUTPUT));
}
/* Called after hardware under tensor_gate, BEFORE publishing outputs. The
 * caller excludes command/input/output regions. A cold mutable LOAD may be
 * classified non-retainable; a previously retained LOAD MUST still match. */
static bool a733_rt_live_verify_load(unsigned i,
  const struct a733_rt_a7pm_region_s *region,struct a733_npu_buffer_s *mapped)
{
  if(!g_rt_live || i>=A733_RT_A7PM_MAX_REGIONS || region->size!=mapped->size)return false;
  a733_npu_mem_invalidate(mapped);
  bool same=crc32part(mapped->cpu,region->size,0)==region->crc32;
  g_rt_live->eligible[i]=same;
  return !g_rt_live->retained[i] || same;
}
static int a733_rt_live_run(struct a733_rt_context_s *c,struct a733_npu_run_s *p)
{
  if(!c->live)return p->status=-ENODATA;
  int r=a733_rt_validate_path(p->package_path);if(r)return p->status=r;
  if(strcmp(p->package_path,c->live->path) || p->flags!=A733_NPU_RUN_INPUT_FILE)return p->status=-EINVAL;
  uint64_t import=c->live->profile.import_us;
  memset(&c->live->profile,0,sizeof(c->live->profile));
  c->live->profile.version=A733_NPU_LIVE_ABI;c->live->profile.import_us=import;
  c->live->profile.model_bytes=(uint32_t)c->live->bytes;c->live->profile.model_crc32=c->live->crc;
  c->live->warm=c->live->loaded && g_rt_live_owner==c->live;
  c->live->profile.cache_hit=c->live->warm;
  memset(c->live->retained,0,sizeof(c->live->retained));
  memset(c->live->input_skipped,0,sizeof(c->live->input_skipped));
  g_rt_live=c->live;
  r=a733_rt_run_a7pm(p);
  c->live->profile.status=r;
  c->live->loaded=r==0 && p->hardware_status==0;
  g_rt_live_owner=c->live->loaded?c->live:NULL;
  g_rt_live=NULL;return r;
}
