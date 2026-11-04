/* SPDX-License-Identifier: Apache-2.0 */
#include "cloud_voice_socket.hxx"
#include <cassert>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <ctime>
static unsigned long long cancel_at;
static unsigned long long nowms(){timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000ull+t.tv_nsec/1000000;}
static int cancelled(void *){return cancel_at&&nowms()>=cancel_at?-ECANCELED:0;}
extern "C" void aipet_wipe(void *v,size_t n){volatile unsigned char *p=static_cast<unsigned char *>(v);while(n--)*p++=0;}
int main(int argc,char **argv) {
  assert(argc==3);aipet_cloud::WebSocket socket(cancelled,nullptr);
  int rc=socket.open("localhost","/test",argv[1]);
  if(!std::strcmp(argv[2],"bad-cert")){assert(rc==-EACCES);return 0;}
  if(!std::strcmp(argv[2],"bad-accept")){assert(rc==-EPROTO);return 0;}
  assert(!rc);assert(!socket.send("客户端"));std::string response;
  if(!std::strcmp(argv[2],"idle"))socket.service_budget(1000,150);
  if(!std::strcmp(argv[2],"hard-budget"))socket.service_budget(350,150);
  if(!std::strcmp(argv[2],"slow-first"))socket.service_budget(1000,600);
  auto began=nowms();
  if(!std::strcmp(argv[2],"cancel"))cancel_at=nowms()+200;
  rc=socket.receive(response);
  if(!std::strcmp(argv[2],"masked"))assert(rc==-EPROTO);
  else if(!std::strcmp(argv[2],"cancel"))assert(rc==-ECANCELED&&nowms()-cancel_at<1000);
  else if(!std::strcmp(argv[2],"eof"))assert(rc==-ECONNRESET);
  else if(!std::strcmp(argv[2],"oversize"))assert(rc==-EMSGSIZE);
  else if(!std::strcmp(argv[2],"idle"))assert(rc==-ETIMEDOUT&&nowms()-began>=150&&nowms()-began<700);
  else if(!std::strcmp(argv[2],"hard-budget"))assert(rc==-ETIMEDOUT&&nowms()-began>=350&&nowms()-began<900);
  else if(!std::strcmp(argv[2],"large")){assert(!rc&&response==std::string(100000,'a'));socket.close();}
  else {assert(!rc&&response=="{\"ok\":true}");socket.close();}
  std::puts("PASS actual mbedTLS/WS over loopback; NOT a vendor API or board test.");
}
