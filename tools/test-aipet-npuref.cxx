/* SPDX-License-Identifier: Apache-2.0 */
#include "../board/a733-cubie-a7z/openvela-overlay/apps/system/aipet/voice_npu_check.cxx"
#include <cassert>
struct a733_tensor_client {a733_tensor_client_contract expected;};
static int opens,executes,closes,fail_open,fail_execute,fail_close,want_verify;
int a733_tensor_client_open(const char *,const a733_tensor_client_contract *c,a733_tensor_client **out)
{
  ++opens;if(fail_open)return -EPROTO;
  *out=new a733_tensor_client{*c};return 0;
}
int a733_tensor_client_execute(a733_tensor_client *c,const a733_tensor_client_input *in,
    const a733_tensor_client_output *out,uint32_t timeout,int verify)
{
  ++executes;assert(timeout==5000 && verify==want_verify);
  int saved;assert(pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,&saved)==0 && saved==PTHREAD_CANCEL_DISABLE);
  for(unsigned i=0;i<c->expected.inputs;++i)assert(in[i].data && in[i].bytes==c->expected.input_bytes[i]);
  if(fail_execute)return -EIO;
  for(unsigned i=0;i<c->expected.outputs;++i){assert(out[i].bytes==c->expected.output_bytes[i]);std::memset(out[i].data,0,out[i].bytes);}
  return 0;
}
int a733_tensor_client_close(a733_tensor_client *p){++closes;delete p;return fail_close?-EIO:0;}
int a733_tensor_client_prepare(a733_tensor_client *) {return 0;}
int a733_tensor_client_full_clean(a733_tensor_client *,int) {return 0;}
int a733_tensor_client_crc_legacy(a733_tensor_client *,int) {return 0;}
int a733_tensor_client_get_profile(a733_tensor_client *,a733_tensor_client_profile *p) {*p={};return 0;}
static void fixtures(const char *dir,const char *name,int bad)
{
  a733_tensor_client_contract c{};assert(aipet::contract(name,c)==0);
  assert(mkdir(dir,0700)==0);char p[512];
  for(unsigned i=0;i<c.inputs;++i){
    std::snprintf(p,sizeof(p),"%s/input%u.bin",dir,i);FILE *f=std::fopen(p,"wb");assert(f);
    float sample=bad==2?NAN:.25f;unsigned size=c.input_bytes[i]-(bad==1?4:0);
    for(unsigned j=0;j<size;j+=4)assert(std::fwrite(&sample,4,1,f)==1);
    assert(std::fclose(f)==0);
  }
}
int main(int argc,char **argv)
{
  assert(argc==2);char dir[400];char package[]="/data/test.a7tv",dynamic[]="dynamic";
  const char *names[]={"encoder","decoder","joiner","vocoder","decoder","decoder","decoder","decoder","decoder","decoder"};
  for(int test=0;test<10;++test){
    opens=executes=closes=0;fail_open=test==7;fail_execute=test==8;fail_close=test==9;want_verify=test==4?0:1;
    std::snprintf(dir,sizeof(dir),"%s/case%d",argv[1],test);fixtures(dir,names[test],test==5?1:test==6?2:0);
    char *args[]={const_cast<char *>(names[test]),package,dir,dynamic};
    int result=aipet::voice_npu_check(test==4?4:3,args);
    if(test<5)assert(result==0 && opens==1 && executes==1 && closes==1);
    if(test==5 || test==6)assert(result<0 && opens==0 && executes==0);
    if(test==7)assert(result<0 && opens==1 && executes==0 && closes==0);
    if(test==8 || test==9)assert(result<0 && executes==1 && closes==1);
  }
  assert(aipet::voice_npu_check(0,nullptr)==-EINVAL);
  std::puts("aipet npuref: 11 application contract/file/cancellation/cleanup checks PASS (MOCK)");
  return 0;
}
