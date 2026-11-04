/* SPDX-License-Identifier: Apache-2.0 */
/* Compiles the ACTUAL runtime adapter include against memory/file/HW mocks. */
#define _POSIX_C_SOURCE 200809L
#include "a733_tensor_plan.h"
#include "a733_tensor_ufs_path.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#define NXMUTEX_INITIALIZER 0
typedef int mutex_t;
#define A733_RT_A7PM_MAX_WINDOWS 16
#define A733_RT_A7PM_STLB_SIZE 16384u
#define A733_RT_TRACE_VIP_BASE 0x01000000u
#define A733_RT_TRACE_INIT_OFFSET 0x1000u
#define A733_RT_PAGE_SIZE 4096u
#define A733_RT_END0 0x10000000u
#define A733_NPU_MAX_TIMEOUT_MS 5000u
#define A733_NPU_BUFFER_READ 1u
#define A733_NPU_BUFFER_WRITE 2u
#define A733_NPU_BUFFER_COMMAND 4u
struct a733_npu_buffer_s { void *cpu; uint32_t physical,size,slot; };
struct a733_npu_hw_result_s { uint32_t irq_value,idle,polls; int recovery; };
struct a733_rt_a7pm_window_s { bool active; };
struct a733_rt_context_s { int owner; struct a733_tensor_resident_s *resident; struct a733_tensor_profile_s tensor_profile; };
struct a733_rt_buffer_s {
  struct a733_rt_context_s *owner;
  uint32_t handle,flags,requested,first_page;
};
struct file { int opened; };
static struct { uint32_t pd_entry; } g_rt;
static struct a733_npu_buffer_s g_rt_pool,g_rt_prepared_pool,g_rt_trace,g_rt_prepared_stlb,g_rt_mtlb;
static bool g_rt_cancel;
static const uint32_t g_a733_rt_init_command[]={1,0};
static struct a733_rt_context_s owner={.owner=1},foreign={.owner=2};
static struct a733_rt_buffer_s buffers[2];
static struct a733_tensor_plan_s fixture;
static unsigned char file_data[10000];
static unsigned calls,restores,cleans,invalidates,opens,closes,clean_pool,clean_tables;
static int fail_hw,fail_recovery,fail_guard,fail_map,fail_alloc,corrupt_output,corrupt_tail;
static unsigned allocations;
static int fail_on_alloc,fail_payload_read,fail_file_close;

static uint32_t crc32part(const uint8_t *data,size_t size,uint32_t previous)
{
  uint32_t value=previous;
  while(size--) { value^=*data++; for(unsigned b=0;b<8;++b) value=(value>>1)^((value&1u)?0xedb88320u:0); }
  return value;
}
static void *kmm_zalloc(size_t n) { ++allocations; return fail_alloc || allocations==(unsigned)fail_on_alloc ? NULL : calloc(1,n); }
static void kmm_free(void *p) { free(p); }
static int a733_rt_validate_path(const char *path) { return !strcmp(path,"/data/npu/mock.a7tv") || !strcmp(path,"/data/npu/other.a7tv") ? 0 : -EINVAL; }
static int file_open(struct file *f,const char *path,int flags)
{ (void)flags; if(a733_rt_validate_path(path) && !a733_tensor_ufs_voice_path(path,A733_TENSOR_PATH)) return -ENOENT; f->opened=1; ++opens; return 0; }
static int file_close(struct file *f) { assert(f->opened); f->opened=0; ++closes; return fail_file_close?-EIO:0; }
static off_t file_seek(struct file *f,off_t offset,int whence)
{ assert(f->opened && offset==0 && whence==SEEK_END); return sizeof(file_data); }
static int a733_rt_file_read_exact(struct file *f,off_t offset,void *dst,size_t bytes,uint32_t *crc)
{
  assert(f->opened);
  if(fail_payload_read && offset>=6000)return -ENODATA;
  if(offset<0 || (uint64_t)offset+bytes>sizeof(file_data)) return -ENODATA;
  memcpy(dst,file_data+offset,bytes); if(crc) *crc=crc32part(dst,bytes,*crc); return 0;
}
static uint32_t a733_npu_hw_cid(void) { return A733_TENSOR_CID; }
static struct a733_rt_buffer_s *a733_rt_find_buffer(struct a733_rt_context_s *ctx,uint32_t handle)
{
  for(unsigned i=0;i<2;++i) if(buffers[i].owner==ctx && buffers[i].handle==handle) return &buffers[i];
  return NULL;
}
static int a733_rt_a7pm_map_page(struct a733_rt_a7pm_window_s *windows,unsigned *count,uint32_t vip,uint32_t physical)
{
  assert(!(vip&4095) && !(physical&4095));
  if(fail_map) return -ENOSPC;
  windows[0].active=true; *count=1; return 0;
}
static void a733_rt_a7pm_restore(struct a733_rt_a7pm_window_s *windows,unsigned count)
{ (void)windows; (void)count; ++restores; }
static void a733_npu_mem_clean(const struct a733_npu_buffer_s *b) {
  if(b->cpu==g_rt_prepared_pool.cpu)clean_pool=b->size;
  if(b->cpu==g_rt_prepared_stlb.cpu)clean_tables=b->size;
  ++cleans;
}
static void a733_npu_mem_invalidate(const struct a733_npu_buffer_s *b) { (void)b; ++invalidates; }
static int a733_npu_mem_check_guards(void) { return fail_guard ? -EILSEQ : 0; }
static int a733_npu_hw_execute_initialized(uint32_t pd,uint32_t init,uint32_t command,
                                          uint32_t timeout,volatile bool *cancel,
                                          struct a733_npu_hw_result_s *result)
{
  (void)pd; assert(init==0x01001000 && command==0x01010000 && timeout==500 && !*cancel);
  assert(cleans>=4 && clean_pool>=fixture.pool_bytes && clean_pool<=A733_TENSOR_POOL && clean_tables>=16384); ++calls;
  for(unsigned r=0;r<fixture.header.regions;++r) {
    uint8_t *memory=(uint8_t *)g_rt_prepared_pool.cpu+fixture.pool_offset[r];
    unsigned end=(fixture.region[r].bytes+4095u)&~4095u;
    for(unsigned p=fixture.region[r].bytes;p<end;++p)assert(memory[p]==0);
  }
  for(unsigned p=39*16;p<1024;++p)assert(((uint8_t *)g_rt_prepared_pool.cpu)[fixture.pool_offset[2]+p]==0);
  result->irq_value=0x10; result->idle=0x7fffffff; result->polls=2; result->recovery=fail_recovery;
  if(fail_hw) return -ETIMEDOUT;
  for(unsigned i=0;i<39;++i) {
    unsigned char *src=(unsigned char *)g_rt_prepared_pool.cpu+fixture.pool_offset[1]+i*16;
    unsigned char *dst=(unsigned char *)g_rt_prepared_pool.cpu+fixture.pool_offset[2]+i*16;
    for(unsigned j=0;j<16;++j) dst[j]=src[j]+1;
  }
  if(corrupt_output) ((unsigned char *)g_rt_prepared_pool.cpu)[fixture.pool_offset[2]+38*16+15]^=1;
  if(corrupt_tail) ((unsigned char *)g_rt_prepared_pool.cpu)[fixture.tail_pool_offset]^=1;
  return 0;
}
#include "a733_tensor_runtime.inc"

static void setup(struct a733_tensor_run_s *run)
{
  uint32_t crc;
  a733_rt_tensor_release(&owner); a733_rt_tensor_release(&foreign);
  assert(!g_rt_tensor_resident_bytes);
  memset(&fixture,0,sizeof(fixture)); memset(run,0,sizeof(*run)); memset(file_data,0,sizeof(file_data));
  fixture.header=(struct a733_tensor_header_s){.magic=A733_TENSOR_MAGIC,.version=1,.header_size=128,
    .cid=A733_TENSOR_CID,.total_size=sizeof(file_data),.regions=3,.inputs=39,.outputs=39};
  fixture.region[0]=(struct a733_tensor_region_s){.vip=0x01010000,.file_offset=6000,.bytes=16,.flags=5};
  fixture.region[1]=(struct a733_tensor_region_s){.vip=0x04000000,.file_offset=6016,.bytes=1024,.flags=1};
  fixture.region[2]=(struct a733_tensor_region_s){.vip=0x04010000,.bytes=1024,.flags=2};
  run->version=1; run->timeout_ms=500; run->inputs=run->outputs=39;
  strcpy(run->package_path,"/data/npu/mock.a7tv");
  for(unsigned i=0;i<39;++i) {
    unsigned char value[16]; memset(value,(int)i,16); memcpy(file_data+6016+i*16,value,16);
    fixture.input[i]=(struct a733_tensor_desc_s){.region=1,.offset=i*16,.bytes=16,.rank=1,.dims={4},.crc32=crc32part(value,16,0)};
    memset(value,(int)i+1,16);
    fixture.output[i]=(struct a733_tensor_desc_s){.region=2,.offset=i*16,.bytes=16,.rank=1,.dims={4},.crc32=crc32part(value,16,0)};
    run->input[i]=(struct a733_tensor_ref_s){1,i*16,16,0}; run->output[i]=(struct a733_tensor_ref_s){2,i*16,16,0};
  }
  fixture.region[0].crc32=crc32part(file_data+6000,16,0);
  fixture.region[1].crc32=crc32part(file_data+6016,1024,0);
  crc=crc32part((const uint8_t *)&fixture.header,128,0);
  crc=crc32part((const uint8_t *)fixture.region,96,crc);
  crc=crc32part((const uint8_t *)fixture.input,39*64,crc);
  fixture.header.manifest_crc32=crc32part((const uint8_t *)fixture.output,39*64,crc);
  memcpy(file_data,&fixture.header,128); memcpy(file_data+128,fixture.region,96);
  memcpy(file_data+224,fixture.input,39*64); memcpy(file_data+224+39*64,fixture.output,39*64);
  assert(!a733_tensor_plan_check(&fixture,sizeof(file_data),crc32part));
  buffers[0]=(struct a733_rt_buffer_s){&owner,1,1,1024,1};
  buffers[1]=(struct a733_rt_buffer_s){&owner,2,2,1024,2};
  memcpy((unsigned char *)g_rt_pool.cpu+4096,file_data+6016,1024);
  memset((unsigned char *)g_rt_pool.cpu+8192,0x5a,1024);
  fail_hw=fail_recovery=fail_guard=fail_map=fail_alloc=corrupt_output=corrupt_tail=0;
  allocations=0;fail_on_alloc=fail_payload_read=fail_file_close=0;
  g_rt_tensor_quarantined=false; calls=restores=cleans=invalidates=opens=closes=0;
}
static void untouched(void)
{
  for(unsigned i=0;i<1024;++i) assert(((unsigned char *)g_rt_pool.cpu)[8192+i]==0x5a);
  assert(opens==closes);
}
int main(void)
{
  struct a733_tensor_run_s *run=calloc(1,sizeof(*run));
  struct a733_tensor_query_s query={.version=1};
  unsigned checks=0;
  (void)g_rt_tensor_gate;
  g_rt_pool.cpu=calloc(1,1024*1024); g_rt_pool.physical=0x48000000;
  g_rt_prepared_pool.cpu=calloc(1,A733_TENSOR_POOL); g_rt_prepared_pool.physical=0x49000000;
  g_rt_prepared_pool.size=A733_TENSOR_POOL;
  g_rt_prepared_stlb.cpu=calloc(1,16*16384);g_rt_prepared_stlb.size=16*16384;
  g_rt_mtlb.cpu=calloc(1,4096);g_rt_mtlb.size=4096;
  g_rt_trace.cpu=calloc(1,65536); g_rt_trace.physical=0x4b000000;
  assert(run && g_rt_pool.cpu && g_rt_prepared_pool.cpu && g_rt_trace.cpu);
  setup(run); strcpy(query.package_path,run->package_path);
  assert(!a733_rt_tensor_query(&query) && query.inputs==39 && query.output_bytes[38]==16 && !calls); ++checks;
  setup(run); run->flags=A733_TENSOR_VERIFY_GOLDEN;
  assert(!a733_rt_tensor_run(&owner,run) && run->committed && calls==1 && restores==1);
  for(unsigned i=0;i<39;++i) for(unsigned j=0;j<16;++j)
    assert(((unsigned char *)g_rt_pool.cpu)[8192+i*16+j]==i+1);
  assert(opens==closes); ++checks;
  setup(run);
  for(unsigned step=0;step<8;++step) {
    assert(!a733_rt_tensor_run(&owner,run) && run->committed);
    for(unsigned i=0;i<39;++i) assert(((unsigned char *)g_rt_pool.cpu)[8192+i*16]==i+step+1);
    memcpy((unsigned char *)g_rt_pool.cpu+4096,(unsigned char *)g_rt_pool.cpu+8192,1024);
  }
  ++checks;
  setup(run); assert(a733_rt_tensor_run(&foreign,run)==-EINVAL && !calls && !run->committed); untouched();
  setup(run); run->output[38].handle=1; assert(a733_rt_tensor_run(&owner,run)==-EINVAL && !calls); untouched();
  setup(run); run->input[38].offset=UINT32_MAX; assert(a733_rt_tensor_run(&owner,run)==-EINVAL && !calls); untouched();
  setup(run); run->output[38].bytes=15; assert(a733_rt_tensor_run(&owner,run)==-EINVAL && !calls); untouched(); ++checks;
  setup(run); run->output[38].offset=0; assert(a733_rt_tensor_run(&owner,run)==-EADDRINUSE && !calls); untouched();
  setup(run); buffers[0].flags=3; run->output[38].handle=1;
  assert(a733_rt_tensor_run(&owner,run)==-EADDRINUSE && !calls); untouched(); ++checks;
  setup(run); file_data[6016]^=1;
  assert(a733_rt_tensor_run(&owner,run)==-EBADMSG && !calls); untouched();
  setup(run); fail_map=1; assert(a733_rt_tensor_run(&owner,run)==-ENOSPC && !calls); untouched();
  setup(run); fail_alloc=1; assert(a733_rt_tensor_run(&owner,run)==-ENOMEM && !calls); untouched(); ++checks;
  setup(run); corrupt_output=1; run->flags=1;
  assert(a733_rt_tensor_run(&owner,run)==-EILSEQ && calls==1 && !run->committed); untouched(); ++checks;
  setup(run); fail_guard=1;
  assert(a733_rt_tensor_run(&owner,run)==-EILSEQ && g_rt_tensor_quarantined && !run->committed); untouched();
  setup(run); corrupt_tail=1;
  assert(a733_rt_tensor_run(&owner,run)==-EILSEQ && g_rt_tensor_quarantined && !run->committed); untouched(); ++checks;
  setup(run); fail_hw=1; fail_recovery=-EIO;
  assert(a733_rt_tensor_run(&owner,run)==-ETIMEDOUT && !run->committed && !restores && g_rt_tensor_quarantined); untouched();
  assert(a733_rt_tensor_run(&owner,run)==-EIO && calls==1 && !run->committed);
  setup(run); fail_hw=1;
  assert(a733_rt_tensor_run(&owner,run)==-ETIMEDOUT && g_rt_tensor_quarantined && restores==1); untouched(); ++checks;
  setup(run); run->version=999; run->committed=1;
  assert(a733_rt_tensor_run(&owner,run)==-EINVAL && !run->committed && !calls); untouched(); ++checks;
  setup(run); query.version=1; strcpy(query.package_path,run->package_path);
  assert(!a733_rt_tensor_prepare(&owner,&query) && owner.resident && !calls);
  assert(a733_rt_tensor_prepare(&owner,&query)==-EALREADY);
  assert(g_rt_tensor_resident_bytes==fixture.pool_bytes);
  unsigned cold_opens=opens;
  memset(file_data,0xcc,sizeof(file_data)); /* file replaced/unavailable after snapshot */
  run->flags=A733_TENSOR_RESIDENT|A733_TENSOR_VERIFY_GOLDEN;
  for(unsigned round=0;round<5;++round) {
    assert(!a733_rt_tensor_run(&owner,run) && run->committed && opens==cold_opens);
    for(unsigned i=0;i<39;++i) assert(((unsigned char *)g_rt_pool.cpu)[8192+i*16]==i+1);
    memset(g_rt_prepared_pool.cpu,0xa5,fixture.pool_bytes); /* never keep mutated scratch */
  }
  assert(a733_rt_tensor_run(&foreign,run)==-ENOENT && !run->committed);
  owner.resident->original[fixture.pool_offset[1]]^=1;
  assert(a733_rt_tensor_run(&owner,run)==-EBADMSG && !run->committed);
  a733_rt_tensor_release(&owner); assert(!g_rt_tensor_resident_bytes); ++checks;
  setup(run); file_data[6016]^=1;
  assert(a733_rt_tensor_prepare(&owner,&query)==-EBADMSG && !owner.resident && !g_rt_tensor_resident_bytes);
  setup(run); fail_alloc=1;
  assert(a733_rt_tensor_prepare(&owner,&query)==-ENOMEM && !owner.resident);
  setup(run); g_rt_tensor_resident_bytes=A733_TENSOR_RESIDENT_BUDGET;
  assert(a733_rt_tensor_prepare(&owner,&query)==-ENOMEM && !owner.resident);
  g_rt_tensor_resident_bytes=0; assert(opens==closes); ++checks;
  setup(run);fail_on_alloc=2;
  assert(a733_rt_tensor_prepare(&owner,&query)==-ENOMEM && !owner.resident && !g_rt_tensor_resident_bytes);
  assert(opens==closes);++checks;
  setup(run);fail_payload_read=1;
  assert(a733_rt_tensor_prepare(&owner,&query)==-ENODATA && !owner.resident && !g_rt_tensor_resident_bytes);
  assert(opens==closes);++checks;
  setup(run);fail_file_close=1;
  assert(a733_rt_tensor_prepare(&owner,&query)==-EIO && !owner.resident && !g_rt_tensor_resident_bytes);
  assert(opens==closes);++checks;
  setup(run);assert(!a733_rt_tensor_prepare(&owner,&query));
  strcpy(query.package_path,"/data/npu/other.a7tv");
  assert(!a733_rt_tensor_prepare(&foreign,&query));
  assert(g_rt_tensor_resident_bytes==2*fixture.pool_bytes);
  cold_opens=opens;memset(file_data,0xcc,sizeof(file_data));
  run->flags=A733_TENSOR_RESIDENT|A733_TENSOR_VERIFY_GOLDEN;
  for(unsigned round=0;round<4;++round) {
    struct a733_rt_context_s *ctx=round%2?&foreign:&owner;
    strcpy(run->package_path,round%2?"/data/npu/other.a7tv":"/data/npu/mock.a7tv");
    buffers[0].owner=buffers[1].owner=ctx;
    assert(!a733_rt_tensor_run(ctx,run) && opens==cold_opens && run->committed);
    memset(g_rt_prepared_pool.cpu,0xee,fixture.pool_bytes);
  }
  strcpy(run->package_path,"/data/npu/other.a7tv");
  assert(a733_rt_tensor_run(&owner,run)==-ENOENT && !run->committed);
  a733_rt_tensor_release(&owner);a733_rt_tensor_release(&foreign);
  assert(!g_rt_tensor_resident_bytes && opens==closes);++checks;
  setup(run);assert(!a733_rt_tensor_run(&owner,run));
  assert(clean_pool==fixture.pool_bytes && clean_tables==16384);
  assert(owner.tensor_profile.committed==1 && owner.tensor_profile.status==0 && owner.tensor_profile.us[8]>=owner.tensor_profile.us[4]);
  assert(owner.tensor_profile.clean_bytes==fixture.pool_bytes);
  setup(run);run->flags=A733_TENSOR_FULL_CLEAN;
  assert(!a733_rt_tensor_run(&owner,run) && clean_pool==A733_TENSOR_POOL && clean_tables==16*16384);++checks;
  setup(run);strcpy(query.package_path,run->package_path);assert(!a733_rt_tensor_prepare(&owner,&query));
  /* Cache scratch/padding corruption must not become next-run initialization. */
  for(unsigned r=0;r<fixture.header.regions;++r) {
    unsigned end=(fixture.region[r].bytes+4095u)&~4095u;
    uint8_t *memory=owner.resident->original+fixture.pool_offset[r];
    memset(memory+fixture.region[r].bytes,0x99,end-fixture.region[r].bytes);
  }
  memset(owner.resident->original+fixture.pool_offset[2],0x77,fixture.region[2].bytes);
  memset((uint8_t *)g_rt_prepared_pool.cpu+fixture.pool_bytes,0x69,4096);
  run->flags=A733_TENSOR_RESIDENT|A733_TENSOR_VERIFY_GOLDEN;
  assert(!a733_rt_tensor_run(&owner,run));
  for(unsigned p=0;p<4096;++p)assert(((uint8_t *)g_rt_prepared_pool.cpu)[fixture.pool_bytes+p]==0x69);
  a733_rt_tensor_release(&owner);assert(!g_rt_tensor_resident_bytes);++checks;
  setup(run);run->flags=A733_TENSOR_CRC_LEGACY|A733_TENSOR_VERIFY_GOLDEN;
  assert(!a733_rt_tensor_run(&owner,run) && run->committed);++checks;
  setup(run);strcpy(query.package_path,run->package_path);assert(!a733_rt_tensor_prepare(&owner,&query));
  run->flags=A733_TENSOR_RESIDENT|A733_TENSOR_CRC_LEGACY|A733_TENSOR_VERIFY_GOLDEN;
  assert(!a733_rt_tensor_run(&owner,run) && run->committed);
  owner.resident->original[fixture.pool_offset[1]]^=1;
  assert(a733_rt_tensor_run(&owner,run)==-EBADMSG && !run->committed);
  a733_rt_tensor_release(&owner);++checks;
  const char *ufs_paths[]={"/ufs/npu/general-asr/asr-encoder.a7tv",
    "/ufs/npu/general-asr/asr-decoder.a7tv","/ufs/npu/general-asr/asr-joiner.a7tv",
    "/ufs/npu/general-tts/vocoder.a7tv"};
  for(unsigned i=0;i<4;++i) {
    setup(run);strcpy(run->package_path,ufs_paths[i]);
    memset(&query,0,sizeof(query));query.version=A733_TENSOR_ABI;strcpy(query.package_path,ufs_paths[i]);
    assert(!a733_rt_tensor_query(&query));
    assert(!a733_rt_tensor_prepare(&owner,&query));
    unsigned prepared_opens=opens;run->flags=A733_TENSOR_RESIDENT|A733_TENSOR_VERIFY_GOLDEN;
    assert(!a733_rt_tensor_run(&owner,run) && run->committed && opens==prepared_opens);
    a733_rt_tensor_release(&owner);assert(opens==closes && !g_rt_tensor_resident_bytes);
    setup(run);strcpy(query.package_path,ufs_paths[i]);file_data[6016]^=1;
    assert(a733_rt_tensor_prepare(&owner,&query)==-EBADMSG && !owner.resident);
    assert(opens==closes);++checks;
  }
  const char *denied[]={"/ufs/npu/general-asr/asr-encoder.a7tv.extra",
    "/ufs/npu/general-asr/../general-asr/asr-encoder.a7tv",
    "/ufs/npu/general-tts/other.a7tv","/ufs/npu//general-tts/vocoder.a7tv",
    "/ufs/npu/general-tts\\vocoder.a7tv","/ufs/npu/general-tts/vocoder.a7pm"};
  for(unsigned i=0;i<sizeof(denied)/sizeof(denied[0]);++i) {
    setup(run);strcpy(query.package_path,denied[i]);unsigned before=opens;
    assert(a733_rt_tensor_query(&query)==-EINVAL && opens==before);
    assert(a733_rt_tensor_prepare(&owner,&query)==-EINVAL && opens==before);
  }
  ++checks;
  free(run); free(g_rt_pool.cpu); free(g_rt_prepared_pool.cpu); free(g_rt_trace.cpu);free(g_rt_prepared_stlb.cpu);free(g_rt_mtlb.cpu);
  printf("A7TV actual driver adapter: %u groups PASS (mock HW/files, NOT board acceptance)\n",checks);
  return 0;
}
