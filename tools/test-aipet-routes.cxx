#include "pet_routes.hxx"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
extern "C" int aipet_agent_submit(const char *,uint32_t) { return -1; }
extern "C" int aipet_agent_poll(char *,size_t,uint64_t *) { return -1; }
int main(int argc,char **argv)
{
  auto r=aipet::default_routes();
  assert(r.direct.size()==5 && r.local.size()==2);
  assert(r.direct[0].keywords[0]=="几点");
  auto clock=aipet::expand_clock_template("{time}/{date}/{weekday}");
  assert(clock.find('{')==std::string::npos);
  assert(argc==3);
  assert(aipet::load_routes(argv[1],r));
  assert(r.direct.size()==1 && r.direct[0].replies[0]=="test {weekday}");
  assert(!aipet::load_routes(argv[2],r));
  assert(r.direct.size()==1); /* Failure does not replace the working rules. */
  assert(aipet::routed_ask("HELLO",false,false)==0);
  assert(aipet::routed_ask("写一个故事",false,true)==1);
  aipet::set_local_route_backend([](const char *,char *out,size_t cap) {
    const std::string reply="测试本地接口[开心]";
    if (cap<=reply.size()) return -1;
    reply.copy(out,reply.size()); out[reply.size()]=0; return 0;
  });
  assert(aipet::routed_ask("写一个故事",false,true)==0);
  aipet::set_local_route_backend(nullptr);
  puts("bounded ordered routes/config/template tests passed");
}
