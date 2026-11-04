/* SPDX-License-Identifier: Apache-2.0
 * Real application config/history code; engine/PCM are explicitly MOCKED. */
#define main inherited_stable_config_test
#include "test-local-llm-config.cxx"
#undef main
int main(int argc,char **argv) {
  assert(argc==2);std::filesystem::path root=argv[1],cfg=root/"state/config";
  assert(!aipet::local_llm_control("tokens","128"));
  auto model=root/"mock.gguf";
  {std::ofstream f(model,std::ios::binary);char h[64]={'G','G','U','F',3};f.write(h,sizeof(h));}
  assert(!aipet::local_llm_control("model",model.c_str()));
  {std::ofstream f(cfg/"local_llm.context");f<<"3\n";}
  aipet::install_local_llm_backend();char reply[512]{};
  assert(backend && !prefix_clears && !unloads);
  for(unsigned i=0;i<24;i++) {
    assert(!backend(("独立问题"+std::to_string(i)).c_str(),reply,sizeof(reply)));
    assert(last_turns==0 && tokens==128);
  }
  assert(!std::filesystem::exists(cfg/"local_llm.context.v12"));
  assert(!aipet::local_llm_control("context","3"));
  assert(!prefix_clears && !unloads); // setting history NEVER cold-resets KV
  assert(read(cfg/"local_llm.context.v12")=="3\n");
  for(unsigned i=0;i<5;i++) {
    assert(!backend(("可选记忆"+std::to_string(i)).c_str(),reply,sizeof(reply)));
    assert(last_turns==std::min(i,3u));
  }
  assert(!aipet::local_llm_control("context","0"));
  assert(!backend("重新单轮",reply,sizeof(reply)) && last_turns==0);
  assert(!prefix_clears && !unloads && read(cfg/"local_llm.context")=="3\n");
  {std::ofstream f(cfg/"local_llm.context.v12");f<<"malformed\n";}
  assert(!backend("坏配置仍默认单轮",reply,sizeof(reply)) && last_turns==0);
  assert(!aipet::local_llm_control("context","0"));
  assert(aipet::local_llm_control("context","5"));
  assert(!aipet::local_llm_control("clear") && prefix_clears==1 && !unloads);
  exhausted=true;answer="这句话还没结束";
  assert(backend("不能用截断提速",reply,sizeof(reply))==-EOVERFLOW && !reply[0]);
  exhausted=false;answer="你好";
  assert(!aipet::local_llm_control("off"));
  assert(backend("关闭后",reply,sizeof(reply))==-ENODEV);
  assert(!aipet::local_llm_control("on"));
  assert(!backend("恢复",reply,sizeof(reply)) && last_turns==0);
  assert(!aipet::local_llm_control("unload") && unloads==1);
  puts("PASS: v220 default/history migration isolation, 24 independent turns, opt-in history, warm-prefix policy, full-reply/cancel/off/unload contracts; MOCK engine, NOT board speed");
  return 0;
}
