/* SPDX-License-Identifier: Apache-2.0 */
/* HOST ONLY: replace tensor ioctls with exact Linux native captures, while
 * running the production frontend/state/greedy/backend code. NOT inference
 * on an accelerator, a CPU model or a production provider. */
#include "voice_general_asr.hxx"
#include "voice_general_contract.hxx"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>
#include <limits>
using Bytes=std::vector<unsigned char>;
struct Entry {std::string role,name;std::vector<Bytes> input,output;};
static std::vector<Entry> captures;
static unsigned clients=0,calls=0;
static bool bad=false;
struct a733_tensor_client {std::string role;a733_tensor_client_contract shape;};
static Bytes read(const std::string &name) {
  std::ifstream f(name,std::ios::binary);assert(f);
  return Bytes(std::istreambuf_iterator<char>(f),{});
}
extern "C" int a733_tensor_client_open(const char *name,
    const a733_tensor_client_contract *shape,a733_tensor_client **out) {
  std::string n=name;auto p=n.rfind("/asr-");assert(p!=std::string::npos);
  std::string role=n.substr(p+5);assert(role.size()>5 && role.substr(role.size()-5)==".a7tv");
  role.resize(role.size()-5);
  a733_tensor_client_contract expected{};
  assert(!aipet::voice_general_contract(("asr-"+role).c_str(),expected));
  assert(!std::memcmp(&expected,shape,sizeof(expected)));
  *out=new a733_tensor_client{role,*shape};++clients;return 0;
}
extern "C" int a733_tensor_client_close(a733_tensor_client *s) {
  assert(s && clients);--clients;delete s;return 0;
}
extern "C" int a733_tensor_client_prepare(a733_tensor_client *s) {assert(s);return 0;}
extern "C" int a733_tensor_client_execute(a733_tensor_client *s,
    const a733_tensor_client_input *input,const a733_tensor_client_output *output,
    uint32_t timeout,int golden) {
  assert(timeout>0 && timeout<=5000 && golden==0);++calls;
  for(const auto &e:captures) {
    if(e.role!=s->role)continue;
    assert(e.input.size()==s->shape.inputs && e.output.size()==s->shape.outputs);
    bool equal=true;
    for(unsigned i=0;i<e.input.size();i++) {
      assert(input[i].bytes==e.input[i].size());
      if(std::memcmp(input[i].data,e.input[i].data(),input[i].bytes)){equal=false;break;}
    }
    if(!equal)continue;
    for(unsigned i=0;i<e.output.size();i++) {
      assert(output[i].bytes==e.output[i].size());
      std::memcpy(output[i].data,e.output[i].data(),output[i].bytes);
    }
    if(bad){float nan=std::numeric_limits<float>::quiet_NaN();std::memcpy(output[0].data,&nan,4);}
    return 0;
  }
  std::fprintf(stderr,"no captured %s input match, call=%u\n",s->role.c_str(),calls);
  return -EILSEQ;
}
static long long deadline() {
  timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);
  return static_cast<long long>(t.tv_sec)*1000000+t.tv_nsec/1000+110000000;
}
static int cancel(void *p){return *static_cast<int *>(p);}
int main(int argc,char **argv) {
  assert(argc==3);std::string root=argv[2];
  std::ifstream manifest(root+"/calls.txt");assert(manifest);
  std::string role,folder;
  while(manifest>>role>>folder) {
    Entry e{role,folder,{},{}};a733_tensor_client_contract shape{};
    assert(!aipet::voice_general_contract(("asr-"+role).c_str(),shape));
    for(unsigned i=0;i<shape.inputs;i++)e.input.push_back(read(folder+"/input/input"+std::to_string(i)+".bin"));
    for(unsigned i=0;i<shape.outputs;i++)e.output.push_back(read(folder+"/output/output"+std::to_string(i)+".bin"));
    captures.push_back(std::move(e));
  }
  assert(captures.size()==216);
  const char *names[]={"cmd_open_light_16k","cmd_time_16k","neg_silence_3s_16k"};
  const char *expected[]={"帮我打开灯","现在几点了",""};
  aipet::VoiceGeneralAsr *reuse=nullptr;
  for(unsigned c=0;c<6;c++) {
    auto pcm=read(root+"/"+names[c%3]+".s16");aipet::VoiceGeneralAsr *s=reuse;
    int rc=0;
    if(!s)rc=aipet::voice_general_asr_open(argv[1],deadline(),nullptr,nullptr,&s);
    else rc=aipet::voice_general_asr_reset(s,deadline(),nullptr,nullptr);
    reuse=s;
    if(rc)std::fprintf(stderr,"ASR open failed=%d\n",rc);
    assert(!rc);
    assert(clients==3);
    if(c==0) {
      for(unsigned rejected=0;rejected<3;rejected++) {
        for(unsigned block=0;block<120;block++)
          assert(!aipet::voice_general_asr_accept(s,pcm.data()+block*320,320));
        unsigned before_discard=calls;
        assert(!aipet::voice_general_asr_discard(s));
        assert(calls==before_discard && clients==3);
        char rejected_text[64]="no publication";
        assert(aipet::voice_general_asr_text(s,rejected_text,sizeof(rejected_text))==-EBUSY);
        assert(!std::strcmp(rejected_text,"no publication"));
        assert(aipet::voice_general_asr_finish(s)==-EBUSY && calls==before_discard);
        assert(aipet::voice_general_asr_discard(s)==-EALREADY);
        assert(!aipet::voice_general_asr_reset(s,deadline(),nullptr,nullptr));
        // Existing cold [blank,blank] decoder initialization is one tensor
        // call for the NEW turn, not final padding/recognition of the old one.
        assert(calls==before_discard+1 && clients==3);
      }
      puts("PASS v224: three partial-state discards perform zero final tensor calls; no text; same clients and cold next-state replay");
    }
    assert(aipet::voice_general_asr_reset(s,deadline(),nullptr,nullptr)==-EBUSY);
    assert(!aipet::voice_general_asr_after_idle(s,deadline()));
    assert(aipet::voice_general_asr_after_idle(s,deadline()+120000000LL)==-EINVAL);
    char text[1024]="untouched";
    assert(aipet::voice_general_asr_text(s,text,sizeof(text))==-EAGAIN);
    assert(!std::strcmp(text,"untouched"));
    for(std::size_t pos=0;pos<pcm.size();) {
      std::size_t n=std::min<std::size_t>(320,pcm.size()-pos);
      assert(!aipet::voice_general_asr_accept(s,pcm.data()+pos,n));pos+=n;
    }
    assert(!aipet::voice_general_asr_finish(s));
    assert(!aipet::voice_general_asr_text(s,text,sizeof(text)));
    assert(!std::strcmp(text,expected[c%3]));
    aipet::VoiceAsrStreamStats stats{};assert(!aipet::voice_general_asr_stats(s,&stats));
    assert(stats.finished && stats.samples==pcm.size()/2);
    std::printf("%s: %s chunks=%u same-three-clients\n",names[c%3],text,stats.chunks);
  }
  assert(!aipet::voice_general_asr_close(reuse) && clients==0);
  aipet::VoiceGeneralAsr *s=nullptr;int stop=1;
  unsigned before=calls;
  assert(aipet::voice_general_asr_open(argv[1],deadline(),cancel,&stop,&s)==-ECANCELED);
  assert(!s && !clients && calls==before);
  stop=0;assert(!aipet::voice_general_asr_open(argv[1],deadline(),cancel,&stop,&s));
  auto pcm=read(root+"/cmd_open_light_16k.s16");bad=true;
  assert(aipet::voice_general_asr_accept(s,pcm.data(),pcm.size())==-EILSEQ);
  assert(aipet::voice_general_asr_reset(s,deadline(),nullptr,nullptr)==-EILSEQ);
  assert(aipet::voice_general_asr_discard(s)==-EILSEQ); // no healthy-discard recovery after tensor error
  char text[32]="untouched";assert(aipet::voice_general_asr_text(s,text,sizeof(text))==-EILSEQ);
  assert(!std::strcmp(text,"untouched"));
  assert(!aipet::voice_general_asr_close(s) && !clients);
  std::printf("PASS: native capture replay + 35-state feedback, cancellation, finite/output gates; mock calls=%u, NPU=NOT_EXECUTED\n",calls);
}
