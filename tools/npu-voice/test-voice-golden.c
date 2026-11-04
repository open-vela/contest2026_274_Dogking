/* SPDX-License-Identifier: Apache-2.0 */
/* Lifecycle and 39-tensor plumbing only; mock computation is NOT inference. */
#define main golden_main
#include "voice-golden.c"
#undef main
#include <assert.h>

struct _vip_buffer { unsigned char data[32]; int mapped; };
struct _vip_network { int unused; };
static struct _vip_network network;
static unsigned count=39,live,runs,prepares,finishes,input_sets,output_sets;
static int fail_query,fail_run,fail_flush;
static vip_buffer ins[MAX_IO],outs[MAX_IO];
vip_uint32_t vip_get_version(void) { return 0x20003; }
vip_status_e vip_init(void) { return VIP_SUCCESS; }
vip_status_e vip_destroy(void) { assert(!live); return VIP_SUCCESS; }
vip_status_e vip_query_hardware(vip_query_hardware_property_e p,vip_uint32_t n,void *v)
{ (void)p; assert(n==4); *(uint32_t *)v=0x1000003b; return VIP_SUCCESS; }
vip_status_e vip_create_network(const void *d,vip_uint32_t n,vip_create_network_type_e t,vip_network *v)
{ (void)d;(void)n;(void)t; *v=&network; return VIP_SUCCESS; }
vip_status_e vip_destroy_network(vip_network n) { assert(n==&network && !live); return VIP_SUCCESS; }
vip_status_e vip_query_network(vip_network n,vip_enum p,void *v)
{ (void)n; assert(p==VIP_NETWORK_PROP_INPUT_COUNT || p==VIP_NETWORK_PROP_OUTPUT_COUNT); *(uint32_t *)v=count; return VIP_SUCCESS; }
static vip_status_e query(vip_network n,vip_uint32_t i,vip_enum p,void *v)
{
  (void)n; assert(i<count);
  if (fail_query && i==37) return VIP_ERROR_FAILURE;
  switch(p) {
    case VIP_BUFFER_PROP_DATA_FORMAT: *(vip_buffer_format_e *)v=VIP_BUFFER_FORMAT_FP32; break;
    case VIP_BUFFER_PROP_NUM_OF_DIMENSION: *(uint32_t *)v=2; break;
    case VIP_BUFFER_PROP_SIZES_OF_DIMENSION: ((uint32_t *)v)[0]=2; ((uint32_t *)v)[1]=4; break;
    case VIP_BUFFER_PROP_QUANT_FORMAT: *(vip_buffer_quantize_format_e *)v=VIP_BUFFER_QUANTIZE_NONE; break;
    case VIP_BUFFER_PROP_NAME: strcpy(v,"fake-state"); break;
    default: assert(0);
  }
  return VIP_SUCCESS;
}
vip_status_e vip_query_input(vip_network n,vip_uint32_t i,vip_enum p,void *v) { return query(n,i,p,v); }
vip_status_e vip_query_output(vip_network n,vip_uint32_t i,vip_enum p,void *v) { return query(n,i,p,v); }
vip_status_e vip_create_buffer(vip_buffer_create_params_t *p,vip_uint32_t n,vip_buffer *b)
{ (void)p;(void)n; *b=calloc(1,sizeof(**b)); assert(*b); live++; return VIP_SUCCESS; }
vip_status_e vip_destroy_buffer(vip_buffer b) { assert(!b->mapped); free(b); live--; return VIP_SUCCESS; }
vip_uint32_t vip_get_buffer_size(vip_buffer b) { (void)b; return 32; }
void *vip_map_buffer(vip_buffer b) { assert(!b->mapped); b->mapped=1; return b->data; }
vip_status_e vip_unmap_buffer(vip_buffer b) { assert(b->mapped); b->mapped=0; return VIP_SUCCESS; }
vip_status_e vip_flush_buffer(vip_buffer b,vip_buffer_operation_type_e type)
{ (void)b;(void)type; return fail_flush?VIP_ERROR_FAILURE:VIP_SUCCESS; }
vip_status_e vip_set_network(vip_network n,vip_enum p,void *v)
{ (void)n; assert(p==VIP_NETWORK_PROP_SET_TIME_OUT && *(uint32_t *)v==5000); return VIP_SUCCESS; }
vip_status_e vip_prepare_network(vip_network n) { (void)n; prepares++; return VIP_SUCCESS; }
vip_status_e vip_finish_network(vip_network n) { (void)n; finishes++; return VIP_SUCCESS; }
vip_status_e vip_set_input(vip_network n,vip_uint32_t i,vip_buffer b)
{ (void)n; ins[i]=b; input_sets++; return VIP_SUCCESS; }
vip_status_e vip_set_output(vip_network n,vip_uint32_t i,vip_buffer b)
{ (void)n; outs[i]=b; output_sets++; return VIP_SUCCESS; }
vip_status_e vip_run_network(vip_network n)
{
  (void)n; runs++;
  if(fail_run) return VIP_ERROR_TIMEOUT;
  assert(input_sets==count && output_sets==count);
  for(unsigned i=0;i<count;i++) for(unsigned j=0;j<32;j++) outs[i]->data[j]=ins[i]->data[j]+i;
  return VIP_SUCCESS;
}
int main(int argc,char **argv)
{
  assert(argc==2);
  char dir[1024],file[1100];
  char *inspect[]={"golden","test.nb","--inspect"};
  assert(golden_main(3,inspect)==0 && !live && prepares==0 && runs==0);
  count=65; assert(golden_main(3,inspect)!=0 && !live); count=39;
  fail_query=1; assert(golden_main(3,inspect)!=0 && !live); fail_query=0;
  for(unsigned scenario=0;scenario<6;scenario++) {
    snprintf(dir,sizeof(dir),"%s/run%u",argv[1],scenario);
    char *call[]={"golden","test.nb","--zero",dir};
    prepares=finishes=runs=input_sets=output_sets=0;
    fail_flush=scenario==1; fail_run=scenario==2;
    if(scenario==3) assert(mkdir(dir,0700)==0);
    if(scenario==4) call[2]="/missing-inputs";
    if(scenario==5) call[2]=argv[1]; /* deliberately short input0.bin */
    int result=golden_main(4,call);
    assert(!live && prepares==finishes);
    if(scenario==0) {
      assert(result==0 && runs==1);
      for(unsigned i=0;i<count;i++) {
        snprintf(file,sizeof(file),"%s/output%u.bin",dir,i);
        FILE *f=fopen(file,"rb"); assert(f);
        for(unsigned j=0;j<32;j++) assert(fgetc(f)==(int)i);
        assert(fgetc(f)==EOF); fclose(f); assert(unlink(file)==0);
      }
    } else assert(result!=0);
    if(scenario!=2 && scenario!=0) assert(runs==0);
    assert(rmdir(dir)==0);
  }
  prepares=finishes=runs=input_sets=output_sets=0; fail_flush=fail_run=0;
  snprintf(dir,sizeof(dir),"%s/repeated",argv[1]);
  char *repeat[]={"golden","test.nb","--zero",dir,"--repeat","3"};
  assert(golden_main(6,repeat)==0 && !live && runs==3 && prepares==1 && finishes==1);
  for(unsigned step=0;step<3;step++) {
    for(unsigned i=0;i<count;i++) {
      snprintf(file,sizeof(file),"%s/step%03u/output%u.bin",dir,step,i);
      FILE *f=fopen(file,"rb"); assert(f);
      for(unsigned j=0;j<32;j++) assert(fgetc(f)==(int)i);
      assert(fgetc(f)==EOF); fclose(f); assert(unlink(file)==0);
    }
    snprintf(file,sizeof(file),"%s/step%03u",dir,step); assert(rmdir(file)==0);
  }
  assert(rmdir(dir)==0);
  const char *bad[]={"0","21","-1","2bad","9999999999999999999999999",""};
  for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
    repeat[5]=(char *)bad[i]; assert(golden_main(6,repeat)==2 && !live && runs==3);
  }
  puts("39-input/output + negative lifecycle/cleanup tests: PASS (MOCK, not NPU)");
  return 0;
}
