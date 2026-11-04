/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#define open mock_open
#define close mock_close
#define ioctl mock_ioctl
#include "a733_tensor_client.c"
#undef open
#undef close
#undef ioctl
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

static unsigned opens,closes,allocs,runs,prepares;
static int no_extension,wrong_contract,allocation_failure,hardware_failure,bad_result,bad_profile;
static void *storage[2];
static struct a733_tensor_client_contract contract;
int mock_open(const char *path,int flags,...)
{ assert(!strcmp(path,"/dev/npu0") && flags==(O_RDWR|O_CLOEXEC)); ++opens; return 42; }
int mock_close(int fd)
{
  assert(fd==42); ++closes;
  for(unsigned i=0;i<2;++i) { free(storage[i]); storage[i]=NULL; }
  return 0;
}
int mock_ioctl(int fd,unsigned long command,...)
{
  unsigned long argument; va_list args; assert(fd==42);
  va_start(args,command); argument=va_arg(args,unsigned long); va_end(args);
  if(command==A733_NPUIOC_TENSOR_QUERY || command==A733_NPUIOC_TENSOR_PREPARE) {
    if(command==A733_NPUIOC_TENSOR_PREPARE) ++prepares;
    struct a733_tensor_query_s *q=(void *)(uintptr_t)argument;
    if(no_extension) { errno=ENOTTY; return -1; }
    assert(q->version==1 && !strcmp(q->package_path,"/data/npu/mock.a7tv"));
    q->status=0; q->inputs=contract.inputs; q->outputs=contract.outputs;
    memcpy(q->input_bytes,contract.input_bytes,sizeof(q->input_bytes));
    memcpy(q->output_bytes,contract.output_bytes,sizeof(q->output_bytes));
    if(wrong_contract) --q->output_bytes[38];
    return 0;
  }
  if(command==A733_NPUIOC_ALLOC) {
    struct a733_npu_alloc_s *a=(void *)(uintptr_t)argument;
    ++allocs;
    if(allocation_failure && allocs==2) { errno=ENOMEM; return -1; }
    assert(allocs<=2 && a->size==39*64);
    storage[allocs-1]=calloc(1,a->size); assert(storage[allocs-1]);
    a->allocated=a->size; a->handle=allocs; a->cpu_address=(uint64_t)(uintptr_t)storage[allocs-1];
    return 0;
  }
  if(command==A733_NPUIOC_TENSOR_PROFILE) {
    struct a733_tensor_profile_s *p=(void *)(uintptr_t)argument;
    assert(p->version==1);p->status=0;p->committed=bad_profile?0:1;
    p->us[4]=12;p->us[8]=99;p->clean_bytes=204800;return 0;
  }
  assert(command==A733_NPUIOC_TENSOR_RUN);
  struct a733_tensor_run_s *r=(void *)(uintptr_t)argument;
  assert(r->version==1 && r->inputs==39 && r->outputs==39);
  ++runs;
  if(hardware_failure) { r->status=r->hardware_status=-ETIMEDOUT; r->committed=0; errno=ETIMEDOUT; return -1; }
  for(unsigned i=0;i<39;++i) {
    assert(r->input[i].handle==1 && r->input[i].offset==i*64 && r->input[i].bytes==16);
    assert(r->output[i].handle==2 && r->output[i].offset==i*64 && r->output[i].bytes==16);
    for(unsigned j=0;j<16;++j) ((unsigned char *)storage[1])[i*64+j]=((unsigned char *)storage[0])[i*64+j]+1;
  }
  r->committed=1; r->status=r->hardware_status=0;
  if(bad_result) r->output[38].offset=UINT32_MAX;
  return 0;
}
static void setup(void)
{
  opens=closes=allocs=runs=prepares=0; no_extension=wrong_contract=allocation_failure=hardware_failure=bad_result=bad_profile=0;
  assert(!storage[0] && !storage[1]);
  memset(&contract,0,sizeof(contract)); contract.inputs=contract.outputs=39;
  for(unsigned i=0;i<39;++i) contract.input_bytes[i]=contract.output_bytes[i]=16;
}
int main(void)
{
  struct a733_tensor_client *s;
  struct a733_tensor_client_input in[39];
  struct a733_tensor_client_output out[39];
  unsigned char input[39][16],output[39][16];
  unsigned checks=0;
  for(unsigned i=0;i<39;++i) { in[i]=(struct a733_tensor_client_input){input[i],16}; out[i]=(struct a733_tensor_client_output){output[i],16}; }
  setup(); assert(!a733_tensor_client_open("/data/npu/mock.a7tv",&contract,&s));
  for(unsigned i=0;i<39;++i) memset(input[i],(int)i,16);
  for(unsigned step=0;step<8;++step) {
    assert(!a733_tensor_client_execute(s,in,out,500,0));
    for(unsigned i=0;i<39;++i) for(unsigned j=0;j<16;++j) assert(output[i][j]==i+step+1);
    memcpy(input,output,sizeof(input));
  }
  assert(runs==8 && !a733_tensor_client_close(s) && opens==closes); ++checks;
  setup(); no_extension=1;
  assert(a733_tensor_client_open("/data/npu/mock.a7tv",&contract,&s)==-ENOTTY && !s && opens==closes && !runs); ++checks;
  setup(); wrong_contract=1;
  assert(a733_tensor_client_open("/data/npu/mock.a7tv",&contract,&s)==-EPROTO && !s && opens==closes); ++checks;
  setup(); allocation_failure=1;
  assert(a733_tensor_client_open("/data/npu/mock.a7tv",&contract,&s)==-ENOMEM && !s && opens==closes && !storage[0]); ++checks;
  setup(); assert(!a733_tensor_client_open("/data/npu/mock.a7tv",&contract,&s));
  memset(output,0x5a,sizeof(output)); out[38].bytes=15;
  assert(a733_tensor_client_execute(s,in,out,500,0)==-EINVAL && !runs && !s->poisoned);
  out[38].bytes=16; out[38].data=output[0];
  assert(a733_tensor_client_execute(s,in,out,500,0)==-EADDRINUSE && !runs);
  out[38].data=output[38]; hardware_failure=1;
  assert(a733_tensor_client_execute(s,in,out,500,0)==-ETIMEDOUT && runs==1);
  assert(a733_tensor_client_execute(s,in,out,500,0)==-EIO && runs==1);
  for(unsigned i=0;i<39;++i) for(unsigned j=0;j<16;++j) assert(output[i][j]==0x5a);
  assert(!a733_tensor_client_close(s)); ++checks;
  setup();assert(!a733_tensor_client_open("/data/npu/mock.a7tv",&contract,&s));
  assert(!a733_tensor_client_full_clean(s,1));
  assert(!a733_tensor_client_execute(s,in,out,500,0) && (s->run.flags&A733_TENSOR_FULL_CLEAN));
  struct a733_tensor_client_profile profile={0};
  assert(!a733_tensor_client_get_profile(s,&profile) && profile.us[4]==12 && profile.us[8]==99 && profile.clean_bytes==204800);
  bad_profile=1;assert(a733_tensor_client_get_profile(s,&profile)==-EPROTO && s->poisoned && profile.us[8]==99);
  assert(!a733_tensor_client_close(s));++checks;
  assert(a733_tensor_client_crc32("123456789",9,0)==0x2dfd2d88u);++checks;
  setup();assert(!a733_tensor_client_open("/data/npu/mock.a7tv",&contract,&s));
  assert(a733_tensor_client_crc_legacy(s,2)==-EINVAL);
  assert(!a733_tensor_client_crc_legacy(s,1));
  assert(!a733_tensor_client_execute(s,in,out,500,0) && (s->run.flags&A733_TENSOR_CRC_LEGACY));
  assert(!a733_tensor_client_crc_legacy(s,0));
  assert(!a733_tensor_client_execute(s,in,out,500,0) && !(s->run.flags&A733_TENSOR_CRC_LEGACY));
  assert(!a733_tensor_client_close(s));++checks;
  setup(); assert(!a733_tensor_client_open("/data/npu/mock.a7tv",&contract,&s));
  assert(!a733_tensor_client_prepare(s) && prepares==1);
  assert(a733_tensor_client_prepare(s)==-EALREADY && prepares==1);
  assert(!a733_tensor_client_execute(s,in,out,500,0) && (s->run.flags&A733_TENSOR_RESIDENT));
  assert(!a733_tensor_client_close(s)); ++checks;
  setup(); assert(!a733_tensor_client_open("/data/npu/mock.a7tv",&contract,&s));
  no_extension=1; assert(a733_tensor_client_prepare(s)==-ENOTTY && s->poisoned);
  assert(a733_tensor_client_execute(s,in,out,500,0)==-EIO && !runs);
  assert(!a733_tensor_client_close(s)); ++checks;
  setup(); assert(!a733_tensor_client_open("/data/npu/mock.a7tv",&contract,&s));
  memset(output,0x5a,sizeof(output)); bad_result=1;
  assert(a733_tensor_client_execute(s,in,out,500,0)==-EPROTO && runs==1);
  for(unsigned i=0;i<39;++i) for(unsigned j=0;j<16;++j) assert(output[i][j]==0x5a);
  assert(!a733_tensor_client_close(s)); ++checks;
  printf("Public NPU tensor client: %u groups PASS (mock ioctl, no board execution)\n",checks);
  return 0;
}
