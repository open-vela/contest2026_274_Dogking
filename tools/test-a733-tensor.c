/* SPDX-License-Identifier: Apache-2.0 */
#include "a733_tensor_plan.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t crc(const uint8_t *data,size_t bytes,uint32_t prev)
{
  uint32_t value=prev;
  while(bytes--) {
    value^=*data++;
    for(unsigned b=0;b<8;++b) value=(value>>1)^((value&1u)?0xedb88320u:0u);
  }
  return value;
}
static struct a733_tensor_plan_s *plan;
static struct a733_tensor_run_s run;
static void manifest(void)
{
  uint32_t value;
  plan->header.manifest_crc32=0;
  value=crc((const uint8_t *)&plan->header,128,0);
  value=crc((const uint8_t *)plan->region,plan->header.regions*32u,value);
  value=crc((const uint8_t *)plan->input,plan->header.inputs*64u,value);
  plan->header.manifest_crc32=crc((const uint8_t *)plan->output,plan->header.outputs*64u,value);
}
static void make_plan(void)
{
  memset(plan,0,sizeof(*plan)); memset(&run,0,sizeof(run));
  plan->header=(struct a733_tensor_header_s){.magic=A733_TENSOR_MAGIC,.version=1,.header_size=128,
    .cid=A733_TENSOR_CID,.total_size=10000,.regions=3,.inputs=39,.outputs=39,.command_region=0};
  strcpy(plan->header.name,"host-synthetic-39-not-a-model");
  plan->region[0]=(struct a733_tensor_region_s){.vip=0x01010000,.file_offset=6000,.bytes=16,.flags=5};
  plan->region[1]=(struct a733_tensor_region_s){.vip=0x04000000,.file_offset=6016,.bytes=1024,.flags=1};
  plan->region[2]=(struct a733_tensor_region_s){.vip=0x04010000,.bytes=1024,.flags=2};
  run.version=1; run.timeout_ms=500; run.inputs=run.outputs=39;
  for(unsigned i=0;i<39;++i) {
    plan->input[i]=(struct a733_tensor_desc_s){.region=1,.offset=i*16,.bytes=16,.rank=1,.dims={4}};
    plan->output[i]=(struct a733_tensor_desc_s){.region=2,.offset=i*16,.bytes=16,.rank=1,.dims={4}};
    run.input[i].bytes=run.output[i].bytes=16;
  }
  manifest();
}
static void invalid(int expected)
{
  manifest(); assert(a733_tensor_plan_check(plan,10000,crc)==expected);
}
int main(void)
{
  uint32_t bytes;
  unsigned checks=0;
  plan=calloc(1,sizeof(*plan)); assert(plan);
  assert(crc((const uint8_t *)"123456789",9,0)==UINT32_C(0x2dfd2d88));
  _Static_assert(sizeof(struct a733_tensor_run_s)==2732,"integer-only run ABI");
  _Static_assert(sizeof(struct a733_tensor_query_s)==1168,"integer-only query ABI");
  make_plan(); assert(!a733_tensor_plan_check(plan,10000,crc));
  assert(plan->pool_bytes==16384 && plan->tail_vip==0x01011000); ++checks;
  plan->header.inputs=65; assert(a733_tensor_header_check(&plan->header,10000,&bytes)==-EPROTO);
  make_plan(); plan->header.reserved[0]=1; invalid(-EPROTO);
  make_plan(); plan->header.total_size=9999; invalid(-EPROTO);
  make_plan(); plan->header.manifest_crc32^=1; assert(a733_tensor_plan_check(plan,10000,crc)==-EBADMSG); ++checks;
  make_plan(); plan->region[1].vip=0x01010000; invalid(-EADDRINUSE);
  make_plan(); plan->region[1].vip=0x01011000; invalid(-EADDRINUSE);
  make_plan(); plan->region[1].vip=0xfffff000; invalid(-EPROTO);
  make_plan(); plan->region[1].vip|=1; invalid(-EPROTO); ++checks;
  make_plan(); plan->region[1].file_offset=6001; invalid(-EADDRINUSE);
  make_plan(); plan->region[1].file_offset=UINT32_MAX; invalid(-EFBIG);
  make_plan(); plan->region[1].bytes=A733_TENSOR_POOL+1; invalid(-EPROTO);
  make_plan(); plan->region[0].flags=7; invalid(-EPROTO); ++checks;
  make_plan(); plan->output[1].offset=1; invalid(-EPROTO);
  make_plan(); plan->output[1].offset=0; invalid(-EADDRINUSE);
  make_plan(); plan->output[0].region=1; plan->region[1].flags=3; invalid(-EADDRINUSE);
  make_plan(); plan->input[0].region=0; invalid(-EPROTO);
  make_plan(); plan->input[0].dims[0]=UINT32_MAX; invalid(-EOVERFLOW);
  make_plan(); plan->input[0].rank=5; invalid(-EPROTO);
  make_plan(); plan->input[0].quant=2; invalid(-EPROTO);
  make_plan(); plan->input[0].format=13; invalid(-ENOTSUP); ++checks;
  /* Byte-exact repeated recurrent transfer of ALL 39 tensors; no numeric
   * conversion. Last state and native padded bytes must never be dropped. */
  {
    unsigned char input[39][16],output[39][16],loaded[1024],produced[1024],command[16];
    void *regions[3]={command,loaded,produced};
    const void *sources[39]; void *destinations[39];
    make_plan();
    for(unsigned i=0;i<39;++i) { sources[i]=input[i]; destinations[i]=output[i]; memset(input[i],(int)i,16); }
    for(unsigned step=0;step<8;++step) {
      memset(output,0x5a,sizeof(output));
      assert(!a733_tensor_input_check(plan,&run,sources,crc));
      a733_tensor_apply(plan,regions,sources); a733_tensor_prefill(plan,regions);
      for(unsigned i=0;i<39;++i) for(unsigned j=0;j<16;++j) produced[i*16+j]=loaded[i*16+j]+1;
      assert(!a733_tensor_output_check(plan,&run,regions,crc));
      a733_tensor_commit(plan,regions,destinations);
      for(unsigned i=0;i<39;++i) for(unsigned j=0;j<16;++j) assert(output[i][j]==i+step+1);
      memcpy(input,output,sizeof(input));
    }
    ++checks;
    run.flags=A733_TENSOR_VERIFY_GOLDEN;
    for(unsigned i=0;i<39;++i) { plan->input[i].crc32=crc(input[i],16,0); plan->output[i].crc32=run.output_crc32[i]; }
    assert(!a733_tensor_input_check(plan,&run,sources,crc));
    input[38][15]^=1; assert(a733_tensor_input_check(plan,&run,sources,crc)==-EBADMSG); input[38][15]^=1;
    run.input[38].bytes=15; assert(a733_tensor_input_check(plan,&run,sources,crc)==-EINVAL); run.input[38].bytes=16;
    memset(output,0x5a,sizeof(output)); produced[38*16+15]^=1;
    assert(a733_tensor_output_check(plan,&run,regions,crc)==-EILSEQ);
    for(unsigned i=0;i<39;++i) for(unsigned j=0;j<16;++j) assert(output[i][j]==0x5a);
    memset(produced,0xa5,sizeof(produced)); run.flags=0;
    assert(a733_tensor_output_check(plan,&run,regions,crc)==-EILSEQ); ++checks;
  }
  /* The REAL validated KWS padded lengths, not only tiny equal-sized mocks. */
  {
    static const uint32_t native[39]={9280,32768,24576,12288,12288,3584,3584,16384,12288,6144,
      6144,3584,3584,8192,6144,3072,3072,3584,3584,8192,3072,3072,3072,3584,3584,8192,
      6144,3072,3072,3584,3584,16384,12288,6144,6144,3584,3584,29184,4};
    const void *input[39]; void *output[39],*regions[3];
    uint32_t in_total=0,out_total=0;
    make_plan();
    for(unsigned i=0;i<39;++i) {
      uint32_t out_size=i ? native[i] : 5120;
      plan->input[i].offset=in_total; plan->input[i].bytes=native[i]; plan->input[i].dims[0]=native[i]/4;
      plan->output[i].offset=out_total; plan->output[i].bytes=out_size; plan->output[i].dims[0]=out_size/4;
      run.input[i].bytes=native[i]; run.output[i].bytes=out_size;
      in_total+=native[i]; out_total+=out_size;
      input[i]=malloc(native[i]); output[i]=malloc(out_size); assert(input[i] && output[i]);
      for(unsigned j=0;j<native[i];++j) ((unsigned char *)input[i])[j]=(unsigned char)(i*13+j*17);
      memset(output[i],0x5a,out_size);
    }
    assert(in_total==303684 && out_total==299524);
    plan->header.total_size=6016+in_total;
    plan->region[1].bytes=in_total; plan->region[2].bytes=out_total; plan->region[2].vip=0x04100000;
    manifest(); assert(!a733_tensor_plan_check(plan,plan->header.total_size,crc));
    regions[0]=calloc(1,16); regions[1]=calloc(1,in_total); regions[2]=calloc(1,out_total);
    assert(regions[0] && regions[1] && regions[2]);
    assert(!a733_tensor_input_check(plan,&run,input,crc));
    a733_tensor_apply(plan,regions,input); a733_tensor_prefill(plan,regions);
    for(unsigned i=0;i<39;++i) {
      unsigned char *src=(unsigned char *)regions[1]+plan->input[i].offset;
      unsigned char *dst=(unsigned char *)regions[2]+plan->output[i].offset;
      assert(!memcmp(src,input[i],native[i]));
      for(unsigned j=0;j<plan->output[i].bytes;++j) dst[j]=src[j]^0x3c;
    }
    assert(!a733_tensor_output_check(plan,&run,regions,crc));
    a733_tensor_commit(plan,regions,output);
    for(unsigned i=0;i<39;++i) {
      for(unsigned j=0;j<plan->output[i].bytes;++j)
        assert(((unsigned char *)output[i])[j]==(((const unsigned char *)input[i])[j]^0x3c));
      free((void *)input[i]); free(output[i]);
    }
    free(regions[0]); free(regions[1]); free(regions[2]); ++checks;
  }
  free(plan);
  printf("A7TV manifest/39-tensor/state/error tests: %u groups PASS (CPU only)\n",checks);
  return 0;
}
