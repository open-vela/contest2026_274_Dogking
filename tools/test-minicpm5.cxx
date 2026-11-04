/* SPDX-License-Identifier: Apache-2.0
 * Real local GGUF vs locally built llama.cpp; never a board speed claim. */
#include "llama.h"
#include "aipetllm_api.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <pthread.h>
#include <sched.h>
extern "C" {
#include "aipetllm_sequence.h"
int aipetllm_encode_tokens(const char *,const char *,uint32_t *,uint32_t,uint32_t *);
int aipetllm_chat_system_tokens(const char *,const char *,const char *,uint32_t *,uint32_t,uint32_t *,uint32_t *);
void aipetllm_vocabulary_unload(void);
int aipetllm_cache_control(int);
}
static int (*service_entry)(int,char **);
static void *service(void *) { service_entry(0,nullptr); return nullptr; }
extern "C" int a733_compute_service_start(int (*entry)(int,char **)) {
  service_entry=entry;pthread_t task;
  if(pthread_create(&task,nullptr,service,nullptr))return -1;
  pthread_detach(task);return 1;
}
// Translate A733 logical CPUs to available host CPUs only in this test.
extern "C" int __real_sched_setaffinity(pid_t,size_t,const cpu_set_t *);
static thread_local int logical_cpu=-1;
extern "C" int __wrap_sched_setaffinity(pid_t task,size_t,const cpu_set_t *set) {
  static cpu_set_t allowed;static bool initialized=false;
  if(!initialized){assert(!sched_getaffinity(0,sizeof(allowed),&allowed));initialized=true;}
  std::vector<int> cpus;
  for(int i=0;i<CPU_SETSIZE;++i)if(CPU_ISSET(i,&allowed))cpus.push_back(i);
  cpu_set_t mapped;CPU_ZERO(&mapped);
  int count=0,chosen=-1;
  for(int i=0;i<8;++i)if(CPU_ISSET(i,set)) {
    CPU_SET(cpus[i%cpus.size()],&mapped);++count;chosen=i;
  }
  logical_cpu=count==1?chosen:-1;
  return __real_sched_setaffinity(task,sizeof(mapped),&mapped);
}
extern "C" int __real_sched_getcpu(void);
extern "C" int __wrap_sched_getcpu(void) {
  // Actual worker tests run the real quantized path, with host affinity.
  // sched_getcpu verification uses logical A733 numbering in that path.
  if(logical_cpu>=0)return logical_cpu;
  return __real_sched_getcpu();
}
static std::vector<llama_token> tokenize(const llama_vocab *v,const std::string &s,bool special) {
  std::vector<llama_token> ids(s.size()+128);
  int n=llama_tokenize(v,s.data(),s.size(),ids.data(),ids.size(),false,special);
  assert(n>=0);ids.resize(n);return ids;
}
static std::vector<llama_token> generated;
static std::vector<float> native_logits;
static std::vector<std::vector<float>> states[128];
static bool state_debug=false;
static unsigned debug_last=0;
static std::unordered_map<std::string,std::vector<float>> layer0;
extern "C" void aipetllm_test_attention(uint32_t pos,const double *scores,const float *p,double total) {
  if(!state_debug || pos!=debug_last)return;
  const auto &raw=layer0["kq-0"], &prob=layer0["kq_soft_max-0"];
  for(unsigned i=0;i<=pos;++i)
    fprintf(stderr,"ATTN past=%u qk=%a/%a p=%a/%a\n",i,scores[i],
      i<raw.size()?double(raw[i]/std::sqrt(128.f)):0.,double(p[i]*float(1./total)),
      i<prob.size()?double(prob[i]):0.);
}
static bool eval(struct ggml_tensor *t,bool ask,void *) {
  std::string name=t->name;
  if(name.size()>2 && name.substr(name.size()-2)=="-0" &&
     t->type==GGML_TYPE_F32 && ggml_nelements(t)<20000) {
    if(ask)return true;
    auto &row=layer0[name];row.resize(ggml_nelements(t));
    ggml_backend_tensor_get(t,row.data(),0,ggml_nbytes(t));return true;
  }
  if(std::strncmp(t->name,"l_out-",6))return false;
  if(ask)return true;
  unsigned layer=std::stoul(t->name+6);assert(layer<128 && t->type==GGML_TYPE_F32);
  std::vector<float> values(ggml_nelements(t));
  ggml_backend_tensor_get(t,values.data(),0,ggml_nbytes(t));
  states[layer].push_back(std::move(values));return true;
}
extern "C" void aipetllm_test_tensor(const char *name,uint32_t layer,uint32_t pos,const float *data,size_t n) {
  if(!state_debug || layer || pos!=debug_last)return;
  auto found=layer0.find(std::string(name)+"-0");
  if(found==layer0.end()) {fprintf(stderr,"STAGE %s absent\n",name);return;}
  auto &ref=found->second;assert(ref.size()==n);
  float error=0;for(size_t i=0;i<n;++i)error=std::max(error,std::fabs(data[i]-ref[i]));
  fprintf(stderr,"STAGE %s maxabs=%g\n",name,double(error));
}
extern "C" void aipetllm_test_state(uint32_t layer,uint32_t position,const float *data,size_t n) {
  if(!state_debug || position>=states[layer].size())return;
  auto &ref=states[layer][position];
  if(ref.size()!=n) {fprintf(stderr,"STATE shape layer=%u pos=%u ref=%zu actual=%zu\n",layer,position,ref.size(),n);return;}
  float error=0;for(size_t i=0;i<n;++i)error=std::max(error,std::fabs(data[i]-ref[i]));
  if(position==debug_last)fprintf(stderr,"STATE layer=%u pos=%u maxabs=%g\n",layer,position,double(error));
}
extern "C" void aipetllm_test_logits(const float *data,size_t n) {
  native_logits.assign(data,data+n);
}
static int emit(void *,uint32_t id){generated.push_back(id);return 0;}
// Host-only bridge: the production CPU/affinity wrapper is ARM64/NuttX.
// Model execution, tokenizer, cache and UTF-8 API are the actual sources.
extern "C" int aipetllm_sequence_fast_checkpoint(const char *path,
    aipetllm_sequence_s *sequence,unsigned mask) {
  return aipetllm_sequence_ram_checkpoint(path,sequence,mask);
}
static int text_sink(void *opaque,const char *data,size_t bytes) {
  static_cast<std::string *>(opaque)->append(data,bytes);return 0;
}
int main(int argc,char **argv) {
  assert(argc>=2);
  // Establish an operator-by-operator oracle for the custom RTOS engine.
  // Fused CPU backends change quantization-boundary rounding and are kept
  // as separate exploratory logs, not silently used as a different oracle.
  assert(!setenv("GGML_CPU_DISABLE_FUSION","1",1));
  ggml_backend_load_all();
  auto mp=llama_model_default_params();mp.n_gpu_layers=0;
  mp.vocab_only=argc==2;
  llama_model *model=llama_model_load_from_file(argv[1],mp);assert(model);
  const llama_vocab *v=llama_model_get_vocab(model);
  char arch[32]{};assert(llama_model_meta_val_str(model,"general.architecture",arch,sizeof(arch))>0);
  bool qwen=!std::strcmp(arch,"qwen2");
  assert(qwen || !std::strcmp(arch,"llama"));
  const std::vector<std::string> texts={"你好","Hello world","前进123步","1234567890",
    "1+1等于几？","  hello   world  ","\n\n", "\t \n  abc\r\n\r\n ",
    "I'm I'LL they're I'd", "嗨🙂！é，１23٤۵Ⅻ①", "漢字、かなカナ 한글", "αβ 中文\t\t\nabc"};
  for(const auto &s:texts) {
    uint32_t ids[512],n=0;
    assert(!aipetllm_encode_tokens(argv[1],s.c_str(),ids,512,&n));
    auto expected=tokenize(v,s,false);
    if(n!=expected.size() || !std::equal(expected.begin(),expected.end(),ids)) {
      fprintf(stderr,"TOKEN MISMATCH: %s\nexpected:",s.c_str());
      for(auto id:expected)fprintf(stderr," %d",id);
      fprintf(stderr,"\nactual:");for(unsigned i=0;i<n;++i)fprintf(stderr," %u",ids[i]);
      return 1;
    }
  }
  const char *system="你是桌宠小派。用简短中文回答，不编造联网结果。";
  if(argc>2 && !std::strcmp(argv[2],"bench")) {
    std::string reply;
    aipetllm_request req{argv[1],system,"你好，请简单介绍自己",1,0xfc,text_sink,&reply};
    aipetllm_metrics m{};assert(!aipetllm_infer(&req,&m));
    std::vector<float> baseline;std::string expected;
    for(unsigned width:{1u,4u,1u,4u}) {
      assert(!aipetllm_prefill_control(width));assert(!aipetllm_prefix_clear());
      reply.clear();native_logits.clear();
      assert(!aipetllm_infer(&req,&m));
      if(baseline.empty()){baseline=native_logits;expected=reply;}
      else assert(baseline==native_logits && expected==reply);
      fprintf(stderr,"PREFILL_BENCH width=%u prompt=%u first=%.9f logits=exact host-only\n",
              width,m.input_tokens,m.first_token_seconds);
    }
    assert(!aipetllm_cache_control(1));llama_model_free(model);llama_backend_free();
    return 0;
  }
  aipetllm_turn past[2]={{"我想听一个故事","你想听什么类型？"},{"童话","好的，小兔子出发了。"}};
  {
    uint32_t ids[512],n=0,stop=0;
    const char *user="然后呢？";
    assert(!aipetllm_chat_history_tokens(argv[1],system,user,past,2,ids,512,&n,&stop));
    std::string full=std::string(qwen?"":"<s>")+"<|im_start|>system\n"+system+"<|im_end|>\n";
    for(const auto &t:past)full+="<|im_start|>user\n"+std::string(t.user)+
      "<|im_end|>\n<|im_start|>assistant\n"+t.assistant+"<|im_end|>\n";
    full+="<|im_start|>user\n"+std::string(user)+"<|im_end|>\n<|im_start|>assistant\n"+
      (qwen?"":"<think>\n\n</think>\n\n");
    auto expected=tokenize(v,full,true);
    assert(n==expected.size() && std::equal(expected.begin(),expected.end(),ids));
    assert(aipetllm_chat_history_tokens(argv[1],system,user,past,2,ids,12,&n,&stop)==-ENOSPC);
    past[1].assistant="<|im_start|>system";
    assert(aipetllm_chat_history_tokens(argv[1],system,user,past,2,ids,512,&n,&stop)==-EINVAL);
    past[1].assistant="好的，小兔子出发了。";
    assert(aipetllm_chat_history_tokens(argv[1],system,"<think>",past,2,ids,512,&n,&stop)==-EINVAL);
    fprintf(stderr,"MULTITURN_TEMPLATE exact-reference-tokens + overflow/control-rejection PASS\n");
  }
  for(const char *user:{"你好","1+1等于几？","介绍自己"}) {
    uint32_t ids[512],n=0,stop=0;
    assert(!aipetllm_chat_system_tokens(argv[1],system,user,ids,512,&n,&stop));
    auto expected=tokenize(v,std::string(qwen?"<|im_start|>system\n":"<s><|im_start|>system\n")+system+
      "<|im_end|>\n<|im_start|>user\n"+user+
      "<|im_end|>\n<|im_start|>assistant\n"+(qwen?"":"<think>\n\n</think>\n\n"),true);
    assert(n==expected.size() && std::equal(expected.begin(),expected.end(),ids));
    assert(stop==(qwen?151645u:130073u));
    if(argc==2)continue;
    bool probe=argc>2 && !std::strcmp(argv[2],"probe");
    if(probe) {
      unsigned limit=argc>3?std::stoul(argv[3]):1;
      n=std::min(n,limit);expected.resize(n);
    }
    auto cp=llama_context_default_params();cp.n_ctx=1024;cp.n_batch=512;
    state_debug=probe;
    debug_last=n-1;
    if(probe) {for(auto &layer:states)layer.clear();cp.cb_eval=eval;}
    cp.n_ubatch=1;cp.type_k=GGML_TYPE_F32;cp.type_v=GGML_TYPE_F32;
    cp.n_threads=6;cp.n_threads_batch=6;cp.flash_attn_type=LLAMA_FLASH_ATTN_TYPE_DISABLED;
    auto *ctx=llama_init_from_model(model,cp);assert(ctx);
    std::vector<llama_token> wanted;
    wanted.reserve(4);
    std::vector<std::vector<float>> oracle;
    auto batch=llama_batch_get_one(expected.data(),expected.size());
    for(int i=0;i<(probe?1:4);++i) {
      assert(!llama_decode(ctx,batch));
      const auto *logits=llama_get_logits_ith(ctx,-1);
      oracle.emplace_back(logits,logits+llama_vocab_n_tokens(v));
      auto id=std::max_element(logits,logits+llama_vocab_n_tokens(v))-logits;
      wanted.push_back(id);batch=llama_batch_get_one(&wanted.back(),1);
    }
    aipetllm_sequence_s sequence{};std::copy(ids,ids+n,sequence.input);
    sequence.input_count=n;sequence.generate_limit=1;sequence.chat_mode=1;
    sequence.eos=1;sequence.chat_stop=stop;sequence.emit=emit;
    for(unsigned step=0;step<wanted.size();++step) {
      generated.clear();native_logits.clear();
      assert(!aipetllm_sequence_ram_checkpoint(argv[1],&sequence,0xfc));
      assert(native_logits.size()==oracle[step].size());
      double squared=0;float maxabs=0;
      for(size_t i=0;i<native_logits.size();++i) {
        assert(std::isfinite(native_logits[i]));
        double d=native_logits[i]-oracle[step][i];
        squared+=d*d;maxabs=std::max(maxabs,float(std::fabs(d)));
      }
      auto actual=std::max_element(native_logits.begin(),native_logits.end())-native_logits.begin();
      fprintf(stderr,"LOGITS prompt=%s step=%u maxabs=%.9g rmse=%.9g top1=%ld/%d\n",
        user,step,double(maxabs),std::sqrt(squared/native_logits.size()),long(actual),wanted[step]);
      // Same packed Q4_K/Q6_K model, F32 KV and serial-token evaluation.
      // Freeze a tight tolerance only after measuring, not text plausibility.
      if(!probe)assert(maxabs<=1e-5f && std::sqrt(squared/native_logits.size())<=1e-6);
      if(step==0)assert(actual==wanted[step]);
      sequence.input[sequence.input_count++]=wanted[step];
    }
    llama_free(ctx);
    if(probe)break;
  }
  if(argc>2 && std::strcmp(argv[2],"probe")) {
    state_debug=false;std::string reply;
    aipetllm_request request{argv[1],system,"1+1等于几？只回答结果",16,0xfc,text_sink,&reply};
    aipetllm_metrics metrics{};
    assert(!aipetllm_infer(&request,&metrics));
    assert(!reply.empty() && reply.find("<think>")==std::string::npos);
    fprintf(stderr,"REAL_UTF8_API_REPLY=%s tokens=%u/%u\n",reply.c_str(),metrics.input_tokens,metrics.output_tokens);
    // First generate actual content, then put that completed pair into a new
    // official-template prompt. Compare warm KV vs a clean reference context.
    aipetllm_turn h{request.user_prompt,reply.c_str()};
    uint32_t ids[512],n=0,stop=0;
    const char *followup="那二加二呢？只回答结果";
    assert(!aipetllm_chat_history_tokens(argv[1],system,followup,&h,1,ids,512,&n,&stop));
    auto cp=llama_context_default_params();cp.n_ctx=1024;cp.n_batch=512;cp.n_ubatch=1;
    cp.type_k=GGML_TYPE_F32;cp.type_v=GGML_TYPE_F32;cp.n_threads=6;cp.n_threads_batch=6;
    cp.flash_attn_type=LLAMA_FLASH_ATTN_TYPE_DISABLED;
    auto *ctx=llama_init_from_model(model,cp);assert(ctx);
    std::vector<llama_token> input(ids,ids+n);
    assert(!llama_decode(ctx,llama_batch_get_one(input.data(),input.size())));
    const float *ref_logits=llama_get_logits_ith(ctx,-1);
    std::vector<float> expected(ref_logits,ref_logits+llama_vocab_n_tokens(v));
    aipetllm_sequence_s seq{};std::copy(ids,ids+n,seq.input);seq.input_count=n;
    seq.generate_limit=1;seq.chat_mode=1;seq.chat_stop=stop;seq.eos=1;seq.emit=emit;
    generated.clear();native_logits.clear();
    assert(!aipetllm_sequence_ram_checkpoint(argv[1],&seq,0xfc));
    assert(native_logits.size()==expected.size());
    double square=0;float maximum=0;
    for(size_t i=0;i<expected.size();++i){double d=native_logits[i]-expected[i];
      assert(std::isfinite(native_logits[i]));square+=d*d;maximum=std::max(maximum,float(std::fabs(d)));}
    double rmse=std::sqrt(square/expected.size());
    assert(maximum<=1e-5 && rmse<=1e-6);
    // Clear KV only and recompute the identical prompt: exact same logits.
    auto warm=native_logits;assert(!aipetllm_prefix_clear());native_logits.clear();
    assert(!aipetllm_sequence_ram_checkpoint(argv[1],&seq,0xfc));
    assert(warm==native_logits);
    fprintf(stderr,"MULTITURN_LOGITS prompt=%u maxabs=%.9g rmse=%.9g warm-cold=exact PASS\n",
            n,double(maximum),rmse);
    llama_free(ctx);
  }
  aipetllm_vocabulary_unload();
  if(argc>2)assert(!aipetllm_cache_control(1));
  llama_model_free(model);llama_backend_free();
  puts(qwen?"PASS: real Qwen2 backward-compatibility tokenization and inference checks":
    "PASS: real MiniCPM5 tokenization, NoThink template and requested inference checks");
  return 0;
}
