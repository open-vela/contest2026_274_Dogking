/* SPDX-License-Identifier: Apache-2.0 */
// REAL packed model + independent llama.cpp logits. Host is NOT A7Z latency.
#define main original_reference_main
#include "test-minicpm5.cxx"
#undef main
#include "../board/a733-cubie-a7z/openvela-overlay/apps/system/aipet/pet_persona.hxx"
int main(int argc,char **argv) {
  assert(argc==2);
  assert(!setenv("GGML_CPU_DISABLE_FUSION","1",1));ggml_backend_load_all();
  auto mp=llama_model_default_params();mp.n_gpu_layers=0;
  auto *model=llama_model_load_from_file(argv[1],mp);assert(model);
  auto *v=llama_model_get_vocab(model);
  aipetllm_turn history[3]={
    {"你是谁","我是小派，你的桌面小伙伴。"},
    {"给我讲个故事","小兔子迷路了，在朋友帮助下找到了家。"},
    {"我叫小明","记住啦，小明，很高兴认识你！"}};
  for(const char *user:{"一加一等于几？只回答结果","我叫什么名字？"}) {
    uint32_t ids[512],n=0,stop=0;
    assert(!aipetllm_chat_history_tokens(argv[1],aipet::pet_persona,user,history,3,ids,512,&n,&stop));
    auto cp=llama_context_default_params();cp.n_ctx=1024;cp.n_batch=512;cp.n_ubatch=1;
    cp.type_k=GGML_TYPE_F32;cp.type_v=GGML_TYPE_F32;cp.n_threads=6;cp.n_threads_batch=6;
    cp.flash_attn_type=LLAMA_FLASH_ATTN_TYPE_DISABLED;
    auto *ctx=llama_init_from_model(model,cp);assert(ctx);
    std::vector<llama_token> tokens(ids,ids+n);
    assert(!llama_decode(ctx,llama_batch_get_one(tokens.data(),tokens.size())));
    const float *p=llama_get_logits_ith(ctx,-1);
    std::vector<float> expected(p,p+llama_vocab_n_tokens(v)),baseline;
    for(unsigned mode=0;mode<3;mode++) {
      assert(!aipetllm_prefill_control(mode==2?4:1));
      if(mode!=1)assert(!aipetllm_prefix_clear());
      aipetllm_sequence_s s{};std::copy(ids,ids+n,s.input);s.input_count=n;
      s.generate_limit=1;s.chat_mode=1;s.chat_stop=stop;s.emit=emit;
      native_logits.clear();generated.clear();
      assert(!aipetllm_sequence_ram_checkpoint(argv[1],&s,0xfc));
      assert(native_logits.size()==expected.size());
      double sq=0;float error=0;
      for(size_t i=0;i<expected.size();i++) {
        double d=native_logits[i]-expected[i];assert(std::isfinite(native_logits[i]));
        sq+=d*d;error=std::max(error,float(std::fabs(d)));
      }
      auto actual=std::max_element(native_logits.begin(),native_logits.end())-native_logits.begin();
      auto wanted=std::max_element(expected.begin(),expected.end())-expected.begin();
      fprintf(stderr,"QUALITY_ORACLE user=%s mode=%u tokens=%u top1=%ld/%ld maxabs=%g rmse=%g\n",
        user,mode,n,long(actual),long(wanted),double(error),std::sqrt(sq/expected.size()));
      assert(actual==wanted && error<=1e-5 && std::sqrt(sq/expected.size())<=1e-6);
      if(mode==0)baseline=native_logits;else assert(baseline==native_logits);
    }
    std::string contextual;
    aipetllm_request req{argv[1],aipet::pet_persona,user,128,0xfc,text_sink,&contextual};
    aipetllm_metrics metrics{};unsigned used=0;
    assert(!aipetllm_infer_chat(&req,history,3,&used,&metrics));
    assert(used==3 && metrics.output_tokens<128);
    if(std::strstr(user,"名字"))assert(contextual.find("小明")!=std::string::npos);
    else assert(contextual.find('2')!=std::string::npos || contextual.find("二")!=std::string::npos);
    fprintf(stderr,"QUALITY_CONTEXT user=%s reply=%s used=%u tokens=%u finish=eos\n",user,contextual.c_str(),used,metrics.output_tokens);
    llama_free(ctx);
  }
  // Actual short-text samples, not a fake semantic-accuracy score.
  for(const char *user:{"你好，你是谁？","我今天很难过，能陪我聊聊吗？","一加一等于几？只回答结果","讲一个三句话、有结尾的小故事。"}) {
    std::string reply;native_logits.clear();
    aipetllm_request req{argv[1],aipet::pet_persona,user,128,0xfc,text_sink,&reply};
    aipetllm_metrics metrics{};assert(!aipetllm_infer(&req,&metrics));
    fprintf(stderr,"QUALITY_SAMPLE user=%s reply=%s tokens=%u finish=%s\n",user,reply.c_str(),
      metrics.output_tokens,metrics.output_tokens==128?"LENGTH_NOT_COMPLETE":"eos");
    assert(!reply.empty() && metrics.output_tokens<128);
  }
  assert(!aipetllm_cache_control(1));aipetllm_vocabulary_unload();
  llama_model_free(model);llama_backend_free();
  puts("PASS: real three-pair history/latest-user, cold/warm/batch logits match independent reference; samples require human quality review");
}
