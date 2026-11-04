/* SPDX-License-Identifier: Apache-2.0 */
/* ACTUAL application orchestration + actual C search, native FILE oracle
 * backend only. No NPU, microphone, cloud, reset or physical action. */
#define AIPET_NPU_TARGET_TEST 1
#define sched_getaffinity mock_sched_getaffinity
#define sched_setaffinity mock_sched_setaffinity
#include "../board/a733-cubie-a7z/openvela-overlay/apps/system/aipet/voice_npu_check.cxx"
#undef sched_getaffinity
#undef sched_setaffinity
#include <cassert>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
struct Native {unsigned model;std::vector<uint32_t> inputs;std::vector<std::vector<char>> outputs;};
static std::vector<Native> natives;
struct a733_tensor_client {unsigned id;bool prepared; a733_tensor_client_contract contract;bool full_clean=false,crc_legacy=false;};
static unsigned opened,closed,calls,prepared;
static int inject;
static cpu_set_t fake_affinity;
static int expected_cpu=6;
int mock_sched_getaffinity(pid_t,size_t size,cpu_set_t *out) noexcept
{ assert(size==sizeof(*out));if(inject==6){errno=EIO;return -1;}*out=fake_affinity;return 0; }
int mock_sched_setaffinity(pid_t,size_t size,const cpu_set_t *in) noexcept
{
  assert(size==sizeof(*in));
  if((inject==7 && CPU_COUNT(in)==1) || (inject==8 && CPU_COUNT(in)>1)){errno=EIO;return -1;}
  fake_affinity=*in;return 0;
}
static void reset_affinity() {CPU_ZERO(&fake_affinity);for(unsigned i=0;i<8;++i)CPU_SET(i,&fake_affinity);}
int a733_tensor_client_full_clean(a733_tensor_client *c,int full){assert(full==0 || full==1);c->full_clean=full!=0;return 0;}
int a733_tensor_client_crc_legacy(a733_tensor_client *c,int legacy){assert(legacy==0 || legacy==1);c->crc_legacy=legacy!=0;return 0;}
int a733_tensor_client_get_profile(a733_tensor_client *c,a733_tensor_client_profile *p)
{
  if(inject==5)return -EIO;
  *p={};p->clean_bytes=c->full_clean?24u*1024u*1024u:c->id==0?8077312:c->id==1?606208:204800;
  p->table_clean_bytes=c->full_clean?262144:16384;p->rebuild_bytes=100;
  return 0;
}
int a733_tensor_client_open(const char *path,const a733_tensor_client_contract *c,a733_tensor_client **out)
{
  unsigned id=std::strstr(path,"encoder.a7tv")?0:std::strstr(path,"decoder.a7tv")?1:2;
  ++opened;*out=new a733_tensor_client{id,false,*c};return 0;
}
int a733_tensor_client_prepare(a733_tensor_client *c)
{
  assert(!c->prepared);++prepared;
  if(inject==1 && prepared==2)return -ENOMEM;
  c->prepared=true;return 0;
}
int a733_tensor_client_close(a733_tensor_client *c) {++closed;delete c;return inject==3?-EIO:0;}
int a733_tensor_client_execute(a733_tensor_client *c,const a733_tensor_client_input *in,
                             const a733_tensor_client_output *out,uint32_t timeout,int verify)
{
  assert(c->prepared && timeout==5000 && !verify);++calls;
  if(expected_cpu<0)assert(CPU_COUNT(&fake_affinity)==8);
  else assert(CPU_COUNT(&fake_affinity)==1 && CPU_ISSET(expected_cpu,&fake_affinity));
  if(inject==4)return -ETIMEDOUT;
  int saved;assert(!pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,&saved) && saved==PTHREAD_CANCEL_DISABLE);
  std::vector<uint32_t> keys;
  for(unsigned i=0;i<c->contract.inputs;++i) {
    assert(in[i].bytes==c->contract.input_bytes[i]);keys.push_back(aipet::crc(in[i].data,in[i].bytes));
  }
  const Native *n=nullptr;
  for(const auto &entry:natives)if(entry.model==c->id && entry.inputs==keys){n=&entry;break;}
  if(!n && (inject==9 || inject==10)) {
    for(unsigned i=0;i<c->contract.outputs;++i)std::memset(out[i].data,0,out[i].bytes);
    if(c->id==2)static_cast<float *>(out[0].data)[0]=20.f; /* mock blank, NOT neural inference */
    if(inject==10)static_cast<float *>(out[0].data)[0]=NAN;
    return 0;
  }
  assert(n && n->outputs.size()==c->contract.outputs);
  for(unsigned i=0;i<c->contract.outputs;++i) {
    assert(out[i].bytes==n->outputs[i].size());std::memcpy(out[i].data,n->outputs[i].data(),out[i].bytes);
  }
  if(inject==2)static_cast<char *>(out[0].data)[0]^=1;
  return 0;
}
int main(int argc,char **argv)
{
  assert(argc==4);std::ifstream map(argv[1]);assert(map);
  reset_affinity();
  size_t count;map>>count;
  for(size_t k=0;k<count;++k) {
    Native n{};unsigned ni,no;map>>n.model>>ni>>no;
    assert(map && ni<=39 && no<=39);
    for(unsigned i=0;i<ni;++i){uint32_t value;map>>value;n.inputs.push_back(value);}
    for(unsigned i=0;i<no;++i) {
      std::string path;map>>path;std::ifstream file(path,std::ios::binary|std::ios::ate);assert(file);
      auto size=file.tellg();assert(size>0 && size<1048576);file.seekg(0);
      n.outputs.emplace_back(static_cast<size_t>(size));file.read(n.outputs.back().data(),size);assert(file);
    }
    natives.push_back(std::move(n));
  }
  for(unsigned scenario=0;scenario<6;++scenario) {
    std::string feature=std::string(argv[2])+"/kws/features"+std::to_string(scenario)+".f32";
    char *args[]={argv[2],const_cast<char *>(feature.c_str())};
    opened=closed=calls=prepared=0;inject=0;
    std::printf("HOST_CASE_BEGIN=%u\n",scenario);
    assert(!aipet::voice_npu_kws(2,args) && opened==3 && closed==3 && prepared==3 && calls>10);
    assert(CPU_COUNT(&fake_affinity)==8);
    std::printf("HOST_CASE_END=%u\n",scenario);
  }
  for(int failure=1;failure<=8;++failure) {
    std::string feature=std::string(argv[2])+"/kws/features0.f32";
    char *args[]={argv[2],const_cast<char *>(feature.c_str())};
    opened=closed=calls=prepared=0;inject=failure;
    reset_affinity();
    assert(aipet::voice_npu_kws(2,args)<0 && opened==closed);
    if(failure==1)assert(!calls && prepared==2);
    if(failure==2)assert(calls==1);
    if(failure==4)assert(calls==1);
    if(failure==5)assert(calls==1);
    if(failure==6 || failure==7)assert(!opened && !calls);
    if(failure!=8)assert(CPU_COUNT(&fake_affinity)==8);
  }
  reset_affinity();
  for(unsigned mode=0;mode<4;++mode) {
    std::string feature=std::string(argv[2])+"/kws/features0.f32";
    char cpu_auto[]="auto",cpu7[]="cpu7",cpu6[]="cpu6",quiet[]="quiet",full[]="fullclean",legacy[]="crclegacy";
    char *args[]={argv[2],const_cast<char *>(feature.c_str()),mode==0?cpu_auto:mode==1?cpu7:cpu6,quiet,mode==3?legacy:full};
    opened=closed=calls=prepared=0;inject=0;expected_cpu=mode==0?-1:mode==1?7:6;
    assert(!aipet::voice_npu_kws(mode==0 || mode==3?5:4,args) && calls==161 && opened==closed && CPU_COUNT(&fake_affinity)==8);
  }
  expected_cpu=6;
  for(int failure=0;failure<3;++failure) {
    std::string path=std::string(argv[3])+"/invalid"+std::to_string(failure)+".f32";
    std::vector<float> values(failure==0?1:29*80,0.f);
    if(failure==1)values[10]=NAN;
    if(failure==2)values[0]=123.456f;
    {std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<char *>(values.data()),values.size()*4);assert(file);}
    char *args[]={argv[2],const_cast<char *>(path.c_str())};
    opened=closed=calls=prepared=0;inject=0;
    assert(aipet::voice_npu_kws(2,args)<0 && !calls && opened==closed);
    if(failure<2)assert(!opened);
  }
  for(unsigned scenario=0;scenario<6;++scenario) {
    std::string feature=std::string(argv[2])+"/kws/features"+std::to_string(scenario)+".f32";
    char *args[]={argv[2],const_cast<char *>(feature.c_str())};
    opened=closed=calls=prepared=0;inject=0;
    std::printf("DYNAMIC_KNOWN_BEGIN=%u\n",scenario);
    assert(!aipet::voice_npu_kws_input(2,args) && opened==closed && calls>10 && CPU_COUNT(&fake_affinity)==8);
    std::printf("DYNAMIC_KNOWN_END=%u\n",scenario);
  }
  std::vector<float> unknown(50*80,0.f);unknown[0]=123.456f;
  opened=closed=calls=prepared=0;inject=9;
  assert(!aipet::voice_npu_kws_memory(argv[2],unknown.data(),50) && calls>0 && opened==closed);
  opened=closed=calls=prepared=0;inject=10;
  assert(aipet::voice_npu_kws_memory(argv[2],unknown.data(),50)==-EILSEQ && calls==1 && opened==closed);
  inject=0;unknown[0]=NAN;opened=closed=calls=prepared=0;
  assert(aipet::voice_npu_kws_memory(argv[2],unknown.data(),50)==-EILSEQ && !opened && !calls);
  unknown[0]=1001.f;assert(aipet::voice_npu_kws_memory(argv[2],unknown.data(),50)==-EILSEQ && !calls);
  assert(aipet::voice_npu_kws_memory(argv[2],unknown.data(),1001)==-EINVAL && !calls);
  std::string feature=std::string(argv[2])+"/kws/features0.f32";
  char *cancel_args[]={argv[2],const_cast<char *>(feature.c_str())};
  sigset_t set,saved_mask;sigemptyset(&set);sigaddset(&set,SIGINT);
  assert(!pthread_sigmask(SIG_BLOCK,&set,&saved_mask));assert(!pthread_kill(pthread_self(),SIGINT));
  opened=closed=calls=prepared=0;inject=0;
  assert(aipet::voice_npu_kws_input(2,cancel_args)==-ECANCELED && !calls && opened==closed && CPU_COUNT(&fake_affinity)==8);
  timespec zero{};assert(sigtimedwait(&set,nullptr,&zero)==SIGINT);
  assert(!pthread_sigmask(SIG_SETMASK,&saved_mask,nullptr));
  std::puts("Actual KWS: strict + known dynamic FILE-oracle six fixtures; synthetic unknown/NaN/admission PASS; NOT NPU execution");
  return 0;
}
