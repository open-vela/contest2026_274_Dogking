/* SPDX-License-Identifier: Apache-2.0 */
/* Included only by the optional A733_NPU_TENSOR_IO experimental runtime.
 * Reuses the established DMA pool/MMU mapper. The GENERAL_ASR opt-in only
 * changes bounded capacities; MMU entry format, clocks and IRQs are unchanged.
 */
#include "a733_tensor_crc.inc"
#include "a733_tensor_ufs_path.h"
static int a733_rt_tensor_validate_path(char *path)
{
  if(a733_tensor_ufs_voice_path(path,A733_TENSOR_PATH))return 0;
  return a733_rt_validate_path(path);
}
static mutex_t g_rt_tensor_gate = NXMUTEX_INITIALIZER;
static bool g_rt_tensor_quarantined;
#ifdef CONFIG_A733_NPU_GENERAL_ASR
#define A733_TENSOR_RESIDENT_BUDGET (96u * 1024u * 1024u)
#else
#define A733_TENSOR_RESIDENT_BUDGET (64u * 1024u * 1024u)
#endif
static uint32_t g_rt_tensor_resident_bytes;
static uint64_t a733_rt_tensor_us(void)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * 1000000u + now.tv_nsec / 1000u;
}
static void a733_rt_tensor_stamp(struct a733_tensor_profile_s *p,
                                 unsigned int phase, uint64_t *mark)
{
  uint64_t now = a733_rt_tensor_us();
  p->us[phase] = now - *mark;
  *mark = now;
}
struct a733_tensor_resident_s
{
  struct a733_tensor_plan_s plan;
  char path[A733_TENSOR_PATH];
  uint8_t *original;
};

static void a733_rt_tensor_release(struct a733_rt_context_s *context)
{
  struct a733_tensor_resident_s *r = context->resident;
  if (!r) return;
  context->resident = NULL;
  g_rt_tensor_resident_bytes -= r->plan.pool_bytes;
  memset(r->original, 0, r->plan.pool_bytes);
  kmm_free(r->original);
  memset(r, 0, sizeof(*r));
  kmm_free(r);
}

struct a733_tensor_work_s
{
  struct a733_tensor_plan_s plan;
  struct a733_tensor_run_s run;
  struct a733_rt_a7pm_window_s windows[A733_RT_A7PM_MAX_WINDOWS];
  struct a733_npu_buffer_s mapped[A733_TENSOR_MAX_REGIONS];
  void *regions[A733_TENSOR_MAX_REGIONS];
  const void *input[A733_TENSOR_MAX_IO];
  void *output[A733_TENSOR_MAX_IO];
};

static int a733_rt_tensor_load_plan(struct file *file,
                                   struct a733_tensor_plan_s *p)
{
  off_t length;
  uint32_t pos;
  int ret;
  length = file_seek(file, 0, SEEK_END);
  if (length < 0) return (int)length;
  ret = a733_rt_file_read_exact(file, 0, &p->header, sizeof(p->header), NULL);
  if (ret < 0) return ret;
  ret = a733_tensor_header_check(&p->header, (uint64_t)length,
                                  &p->descriptor_bytes);
  if (ret < 0) return ret;
  pos = sizeof(p->header);
  ret = a733_rt_file_read_exact(file, pos, p->region,
                                p->header.regions * sizeof(p->region[0]), NULL);
  if (ret < 0) return ret;
  pos += p->header.regions * sizeof(p->region[0]);
  ret = a733_rt_file_read_exact(file, pos, p->input,
                                p->header.inputs * sizeof(p->input[0]), NULL);
  if (ret < 0) return ret;
  pos += p->header.inputs * sizeof(p->input[0]);
  ret = a733_rt_file_read_exact(file, pos, p->output,
                                p->header.outputs * sizeof(p->output[0]), NULL);
  if (ret < 0) return ret;
  ret = a733_tensor_plan_check(p, (uint64_t)length, a733_tensor_crc32part);
  if (ret < 0) return ret;
  /* The existing private init command occupies this VIP page. Never allow
   * a package to replace it or share its speculative-fetch tail page. */
  pos = A733_RT_TRACE_VIP_BASE + A733_RT_TRACE_INIT_OFFSET;
  for (uint32_t i = 0; i < p->header.regions; ++i)
    {
      uint32_t bytes = (p->region[i].bytes + 4095u) & ~4095u;
      if ((uint64_t)p->region[i].vip < (uint64_t)pos + 4096u &&
          (uint64_t)pos < (uint64_t)p->region[i].vip + bytes)
        return -EADDRINUSE;
    }
  if (p->tail_vip == pos) return -EADDRINUSE;
  return 0;
}

static int a733_rt_tensor_query(struct a733_tensor_query_s *request)
{
  struct a733_tensor_work_s *w;
  struct file file;
  char path[A733_TENSOR_PATH];
  uint32_t i;
  int ret;
  memcpy(path, request->package_path, sizeof(path));
  request->status = -EINVAL;
  if (request->version != A733_TENSOR_ABI ||
      !memchr(path, 0, sizeof(path)))
    return -EINVAL;
  request->inputs = request->outputs = 0;
  memset(request->input_bytes, 0, sizeof(request->input_bytes));
  memset(request->output_bytes, 0, sizeof(request->output_bytes));
  memset(request->input_crc32, 0, sizeof(request->input_crc32));
  memset(request->output_crc32, 0, sizeof(request->output_crc32));
  ret = a733_rt_tensor_validate_path(path);
  if (ret < 0) return request->status = ret;
  w = kmm_zalloc(sizeof(*w));
  if (!w) return request->status = -ENOMEM;
  ret = file_open(&file, path, O_RDONLY | O_CLOEXEC);
  if (ret >= 0)
    {
      ret = a733_rt_tensor_load_plan(&file, &w->plan);
      file_close(&file);
    }
  if (ret == 0)
    {
      request->inputs = w->plan.header.inputs;
      request->outputs = w->plan.header.outputs;
      for (i = 0; i < request->inputs; ++i)
        {
          request->input_bytes[i] = w->plan.input[i].bytes;
          request->input_crc32[i] = w->plan.input[i].crc32;
        }
      for (i = 0; i < request->outputs; ++i)
        {
          request->output_bytes[i] = w->plan.output[i].bytes;
          request->output_crc32[i] = w->plan.output[i].crc32;
        }
    }
  kmm_free(w);
  return request->status = ret;
}

/* Serialized with RUN/close. One immutable snapshot per owned fd. Snapshot
 * memory is CPU-only; even failed DMA recovery cannot reference this heap. */
static int a733_rt_tensor_prepare(struct a733_rt_context_s *context,
                                  struct a733_tensor_query_s *q)
{
  struct a733_tensor_resident_s *r;
  struct file file;
  uint32_t checksum, i;
  bool opened = false;
  int ret;
  q->status = -EINVAL;
  if (q->version != A733_TENSOR_ABI ||
      !memchr(q->package_path, 0, sizeof(q->package_path))) return -EINVAL;
  if (g_rt_tensor_quarantined) return q->status = -EIO;
  if (context->resident) return q->status = -EALREADY;
  ret = a733_rt_tensor_validate_path(q->package_path);
  if (ret < 0) return q->status = ret;
  r = kmm_zalloc(sizeof(*r));
  if (!r) return q->status = -ENOMEM;
  memcpy(r->path, q->package_path, sizeof(r->path));
  ret = file_open(&file, r->path, O_RDONLY | O_CLOEXEC);
  if (ret < 0) goto failed;
  opened = true;
  ret = a733_rt_tensor_load_plan(&file, &r->plan);
  if (ret < 0) goto failed;
  if (a733_npu_hw_cid() != r->plan.header.cid) { ret = -ENODEV; goto failed; }
  if (r->plan.pool_bytes > A733_TENSOR_RESIDENT_BUDGET - g_rt_tensor_resident_bytes)
    { ret = -ENOMEM; goto failed; }
  r->original = kmm_zalloc(r->plan.pool_bytes);
  if (!r->original) { ret = -ENOMEM; goto failed; }
  for (i = 0; i < r->plan.header.regions; ++i)
    {
      const struct a733_tensor_region_s *region = &r->plan.region[i];
      if (!(region->flags & A733_TENSOR_LOAD)) continue;
      checksum = 0;
      /* This is owned CPU-only immutable storage, not a DMA buffer. Read
       * exactly once, then use the qualified raw-IEEE dispatcher on ALL bytes.
       * The generic file helper uses libc's slower CRC; do not change it for
       * unrelated loaders or remove the region/input integrity gates. */
      ret = a733_rt_file_read_exact(&file, region->file_offset,
                r->original + r->plan.pool_offset[i], region->bytes, NULL);
      if (ret >= 0)
        checksum = a733_tensor_crc32part(r->original + r->plan.pool_offset[i],
                                       region->bytes, 0);
      if (ret < 0 || checksum != region->crc32)
        { ret = ret < 0 ? ret : -EBADMSG; goto failed; }
    }
  for (i = 0; i < r->plan.header.inputs; ++i)
    {
      const struct a733_tensor_desc_s *d = &r->plan.input[i];
      if (a733_tensor_crc32part(r->original+r->plan.pool_offset[d->region]+d->offset,
                   d->bytes, 0) != d->crc32) { ret = -EBADMSG; goto failed; }
    }
  ret = file_close(&file); opened = false;
  if (ret < 0) goto failed;
  q->inputs = r->plan.header.inputs; q->outputs = r->plan.header.outputs;
  memset(q->input_bytes, 0, sizeof(q->input_bytes));
  memset(q->output_bytes, 0, sizeof(q->output_bytes));
  memset(q->input_crc32, 0, sizeof(q->input_crc32));
  memset(q->output_crc32, 0, sizeof(q->output_crc32));
  for (i = 0; i < q->inputs; ++i)
    { q->input_bytes[i] = r->plan.input[i].bytes; q->input_crc32[i] = r->plan.input[i].crc32; }
  for (i = 0; i < q->outputs; ++i)
    { q->output_bytes[i] = r->plan.output[i].bytes; q->output_crc32[i] = r->plan.output[i].crc32; }
  context->resident = r;
  g_rt_tensor_resident_bytes += r->plan.pool_bytes;
  return q->status = 0;
failed:
  if (opened) file_close(&file);
  if (r->original) { memset(r->original, 0, r->plan.pool_bytes); kmm_free(r->original); }
  kmm_free(r);
  return q->status = ret;
}

static int a733_rt_tensor_resolve(struct a733_rt_context_s *context,
                                  const struct a733_tensor_ref_s *ref,
                                  bool write, void **out)
{
  struct a733_rt_buffer_s *buffer = a733_rt_find_buffer(context, ref->handle);
  if (!buffer || !ref->bytes || ref->reserved ||
      (buffer->flags & A733_NPU_BUFFER_COMMAND) ||
      !(buffer->flags & (write ? A733_NPU_BUFFER_WRITE : A733_NPU_BUFFER_READ)) ||
      ref->offset > buffer->requested || ref->bytes > buffer->requested - ref->offset)
    return -EINVAL;
  *out = (uint8_t *)g_rt_pool.cpu + buffer->first_page * A733_RT_PAGE_SIZE + ref->offset;
  return 0;
}

static bool a733_rt_tensor_ref_overlap(const struct a733_tensor_ref_s *a,
                                        const struct a733_tensor_ref_s *b)
{
  return a->handle == b->handle && (uint64_t)a->offset < (uint64_t)b->offset+b->bytes &&
         (uint64_t)b->offset < (uint64_t)a->offset+a->bytes;
}

static int a733_rt_tensor_run(struct a733_rt_context_s *context,
                              struct a733_tensor_run_s *request)
{
  struct a733_tensor_work_s *w;
  struct a733_npu_hw_result_s hw;
  struct a733_npu_buffer_s init, tail;
  struct a733_npu_buffer_s active_pool, active_tables;
  struct a733_tensor_profile_s *profile = &context->tensor_profile;
  struct file file;
  uint64_t started = a733_rt_tensor_us();
  a733_tensor_crc_t crc = (request->flags & A733_TENSOR_CRC_LEGACY) ?
                          crc32part : a733_tensor_crc32part;
  uint64_t mark = started;
  uint32_t i, j, page, checksum, tail_crc;
  unsigned int windows = 0;
  bool opened = false;
  bool restore = true;
  int ret;
  memset(profile, 0, sizeof(*profile));
  profile->version = A733_TENSOR_ABI;
  profile->status = -EAGAIN;
  request->committed = 0;
  request->status = -EINVAL;
  request->hardware_status = -EAGAIN;
  request->irq_value = request->idle = request->polls = 0;
  memset(request->output_crc32, 0, sizeof(request->output_crc32));
  memset(request->output_changed, 0, sizeof(request->output_changed));
  if (request->version != A733_TENSOR_ABI || !request->inputs ||
      request->inputs > A733_TENSOR_MAX_IO || !request->outputs ||
      request->outputs > A733_TENSOR_MAX_IO || !request->timeout_ms ||
      request->timeout_ms > A733_NPU_MAX_TIMEOUT_MS ||
      (request->flags & ~(A733_TENSOR_VERIFY_GOLDEN | A733_TENSOR_RESIDENT | A733_TENSOR_FULL_CLEAN | A733_TENSOR_CRC_LEGACY)) ||
      !memchr(request->package_path, 0, sizeof(request->package_path))) return -EINVAL;
  if (g_rt_tensor_quarantined) return request->status = -EIO;
  ret = a733_rt_tensor_validate_path(request->package_path);
  if (ret < 0) return request->status = ret;
  w = kmm_zalloc(sizeof(*w));
  if (!w) return request->status = -ENOMEM;
  w->run = *request; /* snapshot descriptors; never re-read mutable user lengths */
  if (!memchr(w->run.package_path, 0, sizeof(w->run.package_path)))
    { ret = -EINVAL; goto out; }
  ret = a733_rt_tensor_validate_path(w->run.package_path);
  if (ret < 0) goto out;
  if (w->run.flags & A733_TENSOR_RESIDENT)
    {
      if (!context->resident || strcmp(context->resident->path, w->run.package_path))
        { ret = -ENOENT; goto out; }
      w->plan = context->resident->plan;
    }
  else
    {
      ret = file_open(&file, w->run.package_path, O_RDONLY | O_CLOEXEC);
      if (ret < 0) goto out;
      opened = true;
      ret = a733_rt_tensor_load_plan(&file, &w->plan);
      if (ret < 0) goto out;
    }
  if (a733_npu_hw_cid() != w->plan.header.cid) { ret = -ENODEV; goto out; }
  if (w->run.inputs != w->plan.header.inputs || w->run.outputs != w->plan.header.outputs)
    { ret = -EPROTO; goto out; }
  for (i = 0; i < w->run.inputs; ++i)
    {
      void *address;
      ret = a733_rt_tensor_resolve(context, &w->run.input[i], false, &address);
      if (ret < 0) goto out;
      w->input[i] = address;
    }
  for (i = 0; i < w->run.outputs; ++i)
    {
      ret = a733_rt_tensor_resolve(context, &w->run.output[i], true, &w->output[i]);
      if (ret < 0) goto out;
      for (j = 0; j < w->run.inputs; ++j)
        if (a733_rt_tensor_ref_overlap(&w->run.output[i], &w->run.input[j]))
          { ret = -EADDRINUSE; goto out; }
      for (j = 0; j < i; ++j)
        if (a733_rt_tensor_ref_overlap(&w->run.output[i], &w->run.output[j]))
          { ret = -EADDRINUSE; goto out; }
    }
  ret = a733_tensor_input_check(&w->plan, &w->run, w->input, crc);
  if (ret < 0) goto out;
  a733_rt_tensor_stamp(profile, 0, &mark);
  /* All software request validation precedes shared prepared-memory writes.
   * File payloads are checked before any MMU change or hardware submission. */
  /* Resident path writes every active byte once: copy verified LOAD data,
   * zero scratch and page padding, then rebuild the END tail below. Do not
   * trust even snapshot padding as scratch initialization. No mutated DMA
   * bytes survive; nonresident path retains the original full zero/read. */
  if (!(w->run.flags & A733_TENSOR_RESIDENT))
    memset(g_rt_prepared_pool.cpu, 0, w->plan.pool_bytes);
  profile->rebuild_bytes = w->plan.pool_bytes;
  for (i = 0; i < w->plan.header.regions; ++i)
    {
      const struct a733_tensor_region_s *region = &w->plan.region[i];
      struct a733_npu_buffer_s *mapped = &w->mapped[i];
      mapped->cpu = (uint8_t *)g_rt_prepared_pool.cpu+w->plan.pool_offset[i];
      mapped->physical = g_rt_prepared_pool.physical+w->plan.pool_offset[i];
      mapped->size = region->bytes;
      mapped->slot = g_rt_prepared_pool.slot;
      w->regions[i] = mapped->cpu;
      if (w->run.flags & A733_TENSOR_RESIDENT)
        {
          uint32_t rounded = (region->bytes + 4095u) & ~4095u;
          if (region->flags & A733_TENSOR_LOAD)
            memcpy(mapped->cpu, context->resident->original+w->plan.pool_offset[i], region->bytes);
          else memset(mapped->cpu, 0, region->bytes);
          memset((uint8_t *)mapped->cpu+region->bytes, 0, rounded-region->bytes);
        }
      if (region->flags & A733_TENSOR_LOAD)
        {
          checksum = 0;
          if (w->run.flags & A733_TENSOR_RESIDENT)
            {
              checksum = crc(mapped->cpu, region->bytes, 0);
              ret = 0;
            }
          else ret = a733_rt_file_read_exact(&file, region->file_offset, mapped->cpu, region->bytes, &checksum);
          if (ret < 0 || checksum != region->crc32)
            { ret = ret < 0 ? ret : -EBADMSG; goto out; }
        }
    }
  for (i = 0; i < w->plan.header.inputs; ++i)
    {
      const struct a733_tensor_desc_s *desc = &w->plan.input[i];
      if (crc((uint8_t *)w->regions[desc->region]+desc->offset, desc->bytes, 0) != desc->crc32)
        { ret = -EBADMSG; goto out; }
    }
  a733_tensor_apply(&w->plan, w->regions, w->input);
  a733_tensor_prefill(&w->plan, w->regions);
  a733_rt_tensor_stamp(profile, 1, &mark);
  for (i = 0; i < w->plan.header.regions; ++i)
    {
      uint32_t bytes = (w->plan.region[i].bytes+4095u)&~4095u;
      for (page = 0; page < bytes; page += 4096u)
        {
          ret = a733_rt_a7pm_map_page(w->windows, &windows, w->plan.region[i].vip+page,
                                      w->mapped[i].physical+page);
          if (ret < 0) goto out;
        }
    }
  tail.cpu = (uint8_t *)g_rt_prepared_pool.cpu+w->plan.tail_pool_offset;
  tail.physical = g_rt_prepared_pool.physical+w->plan.tail_pool_offset;
  tail.size = 4096; tail.slot = g_rt_prepared_pool.slot;
  for (i = 0; i < 4096u/4u; i += 2)
    { ((uint32_t *)tail.cpu)[i] = A733_RT_END0; ((uint32_t *)tail.cpu)[i+1] = 0; }
  tail_crc = crc(tail.cpu, tail.size, 0);
  ret = a733_rt_a7pm_map_page(w->windows, &windows, w->plan.tail_vip, tail.physical);
  if (ret < 0) goto out;
  ret = a733_rt_a7pm_map_page(w->windows, &windows,
                              A733_RT_TRACE_VIP_BASE+A733_RT_TRACE_INIT_OFFSET,
                              g_rt_trace.physical+A733_RT_TRACE_INIT_OFFSET);
  if (ret < 0) goto out;
  init.cpu = (uint8_t *)g_rt_trace.cpu+A733_RT_TRACE_INIT_OFFSET;
  init.physical = g_rt_trace.physical+A733_RT_TRACE_INIT_OFFSET;
  init.size = sizeof(g_a733_rt_init_command); init.slot = g_rt_trace.slot;
  memcpy(init.cpu, g_a733_rt_init_command, init.size);
  a733_rt_tensor_stamp(profile, 2, &mark);
  /* pool_bytes is page-rounded and includes EVERY mapped region, padding
   * and END tail. Only the used prefix of the sequential STLB slots is
   * visible to this command. Preserve full-range mode for hardware A/B. */
  active_pool = g_rt_prepared_pool;
  active_tables = g_rt_prepared_stlb;
  if (!(w->run.flags & A733_TENSOR_FULL_CLEAN))
    {
      active_pool.size = w->plan.pool_bytes;
      active_tables.size = windows * A733_RT_A7PM_STLB_SIZE;
    }
  profile->clean_bytes = active_pool.size;
  profile->table_clean_bytes = active_tables.size;
  a733_npu_mem_clean(&init);
  a733_npu_mem_clean(&active_pool);
  a733_npu_mem_clean(&active_tables);
  a733_npu_mem_clean(&g_rt_mtlb);
  a733_rt_tensor_stamp(profile, 3, &mark);
  g_rt_cancel = false;
  memset(&hw, 0, sizeof(hw));
  ret = a733_npu_hw_execute_initialized(g_rt.pd_entry,
          A733_RT_TRACE_VIP_BASE+A733_RT_TRACE_INIT_OFFSET,
          w->plan.region[w->plan.header.command_region].vip+w->plan.header.command_offset,
          w->run.timeout_ms, &g_rt_cancel, &hw);
  a733_rt_tensor_stamp(profile, 4, &mark);
  w->run.hardware_status = ret;
  w->run.irq_value = hw.irq_value; w->run.idle = hw.idle; w->run.polls = hw.polls;
  if (ret < 0)
    {
      /* Failed recovery may leave DMA active. Keep its backing pool/tables
       * alive and untouched; reject subsequent work until a cold reboot. */
      g_rt_tensor_quarantined = true;
      restore = hw.recovery >= 0;
      goto out;
    }
  a733_npu_mem_invalidate(&tail);
  ret = a733_npu_mem_check_guards();
  if (ret < 0 || crc(tail.cpu, tail.size, 0) != tail_crc)
    { g_rt_tensor_quarantined = true; ret = -EILSEQ; goto out; }
  for (i = 0; i < w->plan.header.regions; ++i) a733_npu_mem_invalidate(&w->mapped[i]);
  ret = a733_tensor_output_check(&w->plan, &w->run, w->regions, crc);
  if (ret < 0) goto out;
  a733_rt_tensor_stamp(profile, 5, &mark);
  /* No partial output publication after failed hardware/guard/CRC validation. */
  a733_tensor_commit(&w->plan, w->regions, w->output);
  w->run.committed = 1;
  a733_rt_tensor_stamp(profile, 6, &mark);
out:
  mark = a733_rt_tensor_us();
  if (restore) a733_rt_a7pm_restore(w->windows, windows);
  if (opened) file_close(&file);
  a733_rt_tensor_stamp(profile, 7, &mark);
  profile->us[8] = a733_rt_tensor_us() - started;
  profile->status = ret;
  profile->committed = w->run.committed;
  w->run.status = ret;
  *request = w->run;
  kmm_free(w);
  return ret;
}
