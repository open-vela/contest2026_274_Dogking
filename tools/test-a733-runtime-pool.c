/* SPDX-License-Identifier: Apache-2.0 */
/* Real driver allocator/reservations + real public client; hardware is MOCK.
 * Extracted C sections are generated from current runtime, not rewritten here.
 */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <nuttx/npu/a733_vip2.h>
#include "runtime-pool-constants.inc"
#include "runtime-pool-structs.inc"
#define OK 0
typedef unsigned int irqstate_t;
struct file { void *f_priv; };
static irqstate_t enter_critical_section(void) { return 0; }
static void leave_critical_section(irqstate_t flags) { assert(flags==0); }
static struct { void *cpu; uint32_t physical; } g_rt_pool;
static struct { uint32_t allocated_pages,allocated_buffers,open_count; } g_rt;
static uint32_t g_rt_inflight;
static bool g_rt_pages[A733_RT_POOL_PAGES];
static struct a733_rt_buffer_s g_rt_buffers[A733_RT_MAX_BUFFERS];
#ifdef CONFIG_A733_NPU_TENSOR_IO
static void a733_rt_tensor_release(struct a733_rt_context_s *c) { assert(!c->resident); }
#endif
#include "runtime-pool-functions.inc"

#define open pool_mock_open
#define close pool_mock_close
#define ioctl pool_mock_ioctl
#include "a733_tensor_client.c"
#undef open
#undef close
#undef ioctl

static struct a733_rt_context_s owner={.active=true,.id=1},foreign={.active=true,.id=2};
static unsigned opens,closes,hardware_calls;
static struct a733_tensor_client_contract contract;
#include "runtime-pool-native.inc"
int pool_mock_open(const char *path,int flags,...)
{
  assert(!strcmp(path,"/dev/npu0") && flags==(O_RDWR|O_CLOEXEC));
  assert(!g_rt.allocated_buffers); ++opens; ++g_rt.open_count; owner.active=true; return 42;
}
int pool_mock_close(int fd)
{
  struct file f={&owner}; assert(fd==42); ++closes;
  return a733_rt_close_impl(&f);
}
int pool_mock_ioctl(int fd,unsigned long command,...)
{
  va_list args; assert(fd==42); va_start(args,command);
  void *arg=(void *)(uintptr_t)va_arg(args,unsigned long); va_end(args);
  if(command==A733_NPUIOC_TENSOR_QUERY) {
    struct a733_tensor_query_s *q=arg;
    q->status=0; q->inputs=contract.inputs; q->outputs=contract.outputs;
    memcpy(q->input_bytes,contract.input_bytes,sizeof(q->input_bytes));
    memcpy(q->output_bytes,contract.output_bytes,sizeof(q->output_bytes)); return 0;
  }
  if(command==A733_NPUIOC_ALLOC) {
    int ret=a733_rt_alloc(&owner,arg);
    if(ret) { errno=-ret; return -1; } return 0;
  }
  assert(command==A733_NPUIOC_TENSOR_RUN); ++hardware_calls;
  struct a733_tensor_run_s *r=arg;
  assert(r->inputs==contract.inputs && r->outputs==contract.outputs);
  for(unsigned i=0;i<contract.inputs;++i) {
    struct a733_rt_buffer_s *in=a733_rt_find_buffer(&owner,r->input[i].handle);
    struct a733_rt_buffer_s *out=a733_rt_find_buffer(&owner,r->output[i].handle);
    assert(in && out && in!=out && !a733_rt_find_buffer(&foreign,r->input[i].handle));
    assert(r->input[i].offset+r->input[i].bytes<=in->requested);
    assert(r->output[i].offset+r->output[i].bytes<=out->requested);
    memset((uint8_t *)g_rt_pool.cpu+out->first_page*4096u+r->output[i].offset,
           (int)(i+1),r->output[i].bytes);
  }
  r->status=r->hardware_status=0; r->committed=1; return 0;
}
static void reserved_ok(void)
{
  assert(g_rt_pages[0]);
  for(unsigned p=A733_RT_TRACE_WEIGHTS_FIRST;p<A733_RT_TRACE_WEIGHTS_FIRST+A733_RT_TRACE_WEIGHTS_PAGES;++p) {
    assert(g_rt_pages[p]);
    for(unsigned i=0;i<4096;++i) assert(((uint8_t *)g_rt_pool.cpu)[p*4096u+i]==0x6d);
  }
}
int main(void)
{
#ifdef CONFIG_A733_NPU_GENERAL_ASR
  assert(A733_RT_POOL_SIZE==8u*1024u*1024u && A733_NPU_ARENA_SIZE==80u*1024u*1024u);
  assert(A733_RT_A7PM_POOL_SIZE==64u*1024u*1024u);
#else
  assert(A733_NPU_ARENA_SIZE==32u*1024u*1024u);
#endif
  g_rt_pool.cpu=calloc(1,A733_RT_POOL_SIZE); assert(g_rt_pool.cpu); g_rt_pool.physical=0x44000000;
  /* Exact boot reservation statements are extracted from the current driver. */
#include "runtime-pool-reservations.inc"
  memset((uint8_t *)g_rt_pool.cpu+A733_RT_TRACE_WEIGHTS_OFFSET,0x6d,
         A733_RT_TRACE_WEIGHTS_PAGES*4096u);
  assert(A733_RT_STAGE_STLB_INDEX==A733_RT_POOL_PAGES);
  assert(A733_RT_STAGE_VIP_BASE==A733_RT_VIP_BASE+A733_RT_POOL_SIZE);
  assert(A733_RT_POOL_PAGES+A733_RT_STAGE_SIZE/4096u<=4096u);
  assert(A733_RT_STAGE_VIP_BASE+A733_RT_STAGE_SIZE<=A733_RT_VIP_BASE+0x01000000u);
  /* Pessimistic bound for all boot allocations, initial memory self-test,
   * alignment and guards. General ASR alone opts into the larger arena. */
  assert(A733_RT_POOL_SIZE+A733_RT_STAGE_SIZE+A733_RT_TRACE_SIZE+
         A733_RT_A7PM_POOL_SIZE+A733_RT_A7PM_MAX_WINDOWS*A733_RT_A7PM_STLB_SIZE+
         2u*16384u+4096u+16u*16384u<=A733_NPU_ARENA_SIZE);
  const uint32_t initial_free=a733_rt_free_bytes();
  assert(initial_free==A733_RT_POOL_SIZE-(1+A733_RT_TRACE_WEIGHTS_PAGES)*4096u);
  contract.inputs=contract.outputs=39;
#ifdef CONFIG_A733_NPU_GENERAL_ASR
  contract.inputs=contract.outputs=36;
  for(unsigned i=0;i<36;++i) { contract.input_bytes[i]=general_encoder_sizes[i]; contract.output_bytes[i]=i?general_encoder_sizes[i]:49152; }
#else
  for(unsigned i=0;i<39;++i) { contract.input_bytes[i]=encoder_sizes[i]; contract.output_bytes[i]=i?encoder_sizes[i]:5120; }
#endif
  struct a733_tensor_client_input input[39]; struct a733_tensor_client_output output[39];
  for(unsigned i=0;i<contract.inputs;++i) {
    input[i]=(struct a733_tensor_client_input){calloc(1,contract.input_bytes[i]),contract.input_bytes[i]};
    output[i]=(struct a733_tensor_client_output){malloc(contract.output_bytes[i]),contract.output_bytes[i]};
    assert(input[i].data && output[i].data);
  }
  for(unsigned iteration=0;iteration<16;++iteration) {
    struct a733_tensor_client *client=NULL;
    int ret=a733_tensor_client_open("/data/npu/mock.a7tv",&contract,&client);
#ifdef CONFIG_A733_NPU_TENSOR_IO
    uint32_t in_bytes=0,out_bytes=0;
    for(unsigned i=0;i<contract.inputs;++i) in_bytes+=(contract.input_bytes[i]+63u)&~63u;
    for(unsigned i=0;i<contract.outputs;++i) out_bytes+=(contract.output_bytes[i]+63u)&~63u;
    assert(!ret && client && g_rt.allocated_buffers==2 &&
      g_rt.allocated_pages==(in_bytes+4095u)/4096u+(out_bytes+4095u)/4096u);
    assert(!a733_tensor_client_execute(client,input,output,5000,0));
    for(unsigned i=0;i<contract.outputs;++i) for(unsigned b=0;b<output[i].bytes;++b)
      assert(((uint8_t *)output[i].data)[b]==i+1);
    assert(!a733_tensor_client_close(client));
#else
    assert(ret==-ENOMEM && !client && !hardware_calls); /* v156 regression reproduced */
#endif
    assert(a733_rt_free_bytes()==initial_free && !g_rt.allocated_pages && !g_rt.allocated_buffers);
    assert(opens==closes); reserved_ok();
  }
  struct a733_npu_alloc_s small[16]={{0}},extra={0};
  for(unsigned i=0;i<16;++i) { small[i].size=4096; small[i].flags=A733_NPU_BUFFER_READ; assert(!a733_rt_alloc(&owner,&small[i])); }
  extra.size=4096; assert(a733_rt_alloc(&owner,&extra)==-ENOSPC);
  for(unsigned i=0;i<16;++i) a733_rt_release_buffer(a733_rt_find_buffer(&owner,small[i].handle));
  assert(a733_rt_free_bytes()==initial_free); reserved_ok();
  for(unsigned i=0;i<contract.inputs;++i) { free((void *)input[i].data); free(output[i].data); }
  free(g_rt_pool.cpu);
  printf("Actual runtime allocator + real client: pool=%u free=%u encoder-rounds=16 hw-mock=%u PASS\n",
         A733_RT_POOL_SIZE,initial_free,hardware_calls);
}
