/* SPDX-License-Identifier: Apache-2.0 */
#include "local_llm.hxx"
#include "pet_routes.hxx"
#include "aipetllm_api.h"
#include <cassert>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
static aipet::LocalRouteBackend backend;
static unsigned calls,tokens,streamed,last_turns,prefix_clears,unloads;
static int failure;
static bool reentry;
static std::string prior_user,prior_reply;
static std::string answer="你好";
static bool exhausted=false,repair_replay=false;
namespace aipet {
void set_local_route_backend(LocalRouteBackend b){backend=b;}
int local_route_stream(const char *data,std::size_t bytes) {
  assert(bytes>0 && data);++streamed;return 0;
}
}
extern "C" int aipetllm_cache_control(int unload){unloads+=unload;return 0;}
extern "C" int aipetllm_prefix_clear(){++prefix_clears;return 0;}
extern "C" int aipetllm_prefill_control(unsigned n){return n==1||n==4?0:-EINVAL;}
extern "C" int aipetllm_generation_control(int){return 0;}
extern "C" int aipetllm_infer_chat(const aipetllm_request *r,const aipetllm_turn *h,
    unsigned count,unsigned *used,aipetllm_metrics *m) {
  ++calls;tokens=r->max_tokens;assert(r->cores==0xfc);
  last_turns=count;*used=count;
  if(count) {prior_user=h[count-1].user;prior_reply=h[count-1].assistant;}
  if(reentry)assert(aipet::local_llm_control("clear"));
  *m={1,exhausted?r->max_tokens:1,0,0};
  const std::string text=repair_replay && !count?"我听见你了，我们聊聊你现在的问题。":answer;
  int rc=r->sink(r->context,text.data(),text.size());return rc?rc:failure;
}
static std::string read(const std::filesystem::path &p) {
  std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};
}
int main(int argc,char **argv) {
  assert(argc==2);std::filesystem::path root=argv[1],state=root/"state",cfg=state/"config";
  assert(!std::filesystem::exists(state));
  assert(!aipet::local_llm_control("tokens","24")); // No online init/directory.
  assert(read(cfg/"local_llm.tokens")=="24\n");
  {std::ofstream f(cfg/"local_llm.tokens.tmp");f<<"KEEP";}
  assert(aipet::local_llm_control("tokens","16"));
  assert(read(cfg/"local_llm.tokens.tmp")=="KEEP" && read(cfg/"local_llm.tokens")=="24\n");
  std::filesystem::remove(cfg/"local_llm.tokens.tmp");
  {std::ofstream f(root/"protected");f<<"SAFE";}
  std::filesystem::create_symlink(root/"protected",cfg/"local_llm.tokens.tmp");
  assert(aipet::local_llm_control("tokens","16"));assert(read(root/"protected")=="SAFE");
  std::filesystem::remove(cfg/"local_llm.tokens.tmp");
  auto model=root/"mock-model.gguf";
  {std::ofstream f(model,std::ios::binary);char header[64]={'G','G','U','F',3};f.write(header,sizeof(header));}
  assert(!aipet::local_llm_control("model",model.c_str()));
  std::string injected=model.string()+"\nOTHER";
  assert(aipet::local_llm_control("model",injected.c_str()));
  assert(read(cfg/"local_llm.model")==model.string()+"\n");
  assert(!aipet::local_llm_preflight() && !calls);
  auto missing=root/"missing.gguf";
  assert(!aipet::local_llm_control("model",missing.c_str()));
  assert(aipet::local_llm_preflight()==-ENOENT && !calls);
  assert(!aipet::local_llm_control("model",model.c_str()));
  assert(!aipet::local_llm_control("preload") && calls==1 && tokens==1 && streamed==1);
  aipet::install_local_llm_backend();assert(backend);char reply[128]{};
  assert(!backend("你好",reply,sizeof(reply)) && calls==2 && tokens==128 && !strcmp(reply,"你好"));
  assert(last_turns==0); // Preload wasn't a dialogue turn.
  assert(!backend("我想听童话",reply,sizeof(reply)) && last_turns==1);
  assert(prior_user=="你好" && prior_reply=="你好");
  failure=125;assert(backend("你好",reply,sizeof(reply))==-ECANCELED);
  assert(!reply[0]);
  failure=0;assert(!backend("你好",reply,sizeof(reply))); // Lock released on error.
  assert(last_turns==2 && prior_user=="我想听童话"); // Cancelled turn not committed.
  assert(!aipet::local_llm_control("preload"));assert(last_turns==0);
  reentry=true;assert(!backend("继续",reply,sizeof(reply)));reentry=false;
  assert(last_turns==3 && prior_user=="你好");
  assert(!aipet::local_llm_control("clear") && prefix_clears==1 && !unloads);
  assert(!backend("新话题",reply,sizeof(reply)) && last_turns==0);
  assert(aipet::local_llm_control("context","5"));
  assert(aipet::local_llm_control("context",""));
  assert(!aipet::local_llm_control("context","0"));
  assert(!backend("单轮",reply,sizeof(reply)) && last_turns==0);
  assert(!backend("依旧单轮",reply,sizeof(reply)) && last_turns==0);
  assert(!aipet::local_llm_control("context","2"));
  assert(!backend("A",reply,sizeof(reply)) && last_turns==0);
  assert(!backend("B",reply,sizeof(reply)) && last_turns==1);
  assert(!backend("C",reply,sizeof(reply)) && last_turns==2);
  assert(!backend("D",reply,sizeof(reply)) && last_turns==2 && prior_user=="C");
  assert(!aipet::local_llm_control("model",model.c_str()));
  assert(!backend("换模型",reply,sizeof(reply)) && last_turns==0);
  assert(!aipet::local_llm_control("off"));unsigned before=calls;
  assert(backend("你好",reply,sizeof(reply))==-ENODEV && calls==before);
  assert(!aipet::local_llm_control("on"));assert(!backend("你好",reply,sizeof(reply)));
  assert(last_turns==0);
  assert(!aipet::local_llm_control("unload") && unloads==1);
  assert(!backend("卸载后",reply,sizeof(reply)) && last_turns==0);
  assert(!aipet::local_llm_control("clear"));
  answer="你好！有什么我可以帮助你的吗？";
  assert(!backend("问候",reply,sizeof(reply)));
  unsigned before_repair=calls,before_stream=streamed;
  repair_replay=true;
  assert(!backend("一加一等于几",reply,sizeof(reply)) && calls==before_repair+2);
  assert(!strcmp(reply,"我听见你了，我们聊聊你现在的问题。") && streamed==before_stream+1);
  repair_replay=false;answer="这句话还没有说完";exhausted=true;
  assert(backend("讲个长故事",reply,sizeof(reply))==-EOVERFLOW && !reply[0]);
  answer="你好";exhausted=false;
  assert(!backend("继续",reply,sizeof(reply)) && prior_user=="一加一等于几");
  unsigned identity_calls=calls;
  assert(!backend("你是谁",reply,sizeof(reply)) && calls==identity_calls);
  assert(strstr(reply,"小派") && !strstr(reply,"MiniCPM"));
  puts("PASS: atomic config, bounded RAM dialogue, stateless preload, clear retaining weights, cancellation/reentry, model/off/unload isolation");
  return 0;
}
