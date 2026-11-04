/* SPDX-License-Identifier: Apache-2.0 */
/* Real transport, fault injection only at POSIX resolver/socket boundary. */
#include "cloud_voice_socket.hxx"
#include <arpa/inet.h>
#include <netdb.h>
#include <dirent.h>
#include <unistd.h>
#include <cerrno>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <ctime>
static int sockets_failed;static bool dual;
extern "C" int __real_socket(int,int,int);
extern "C" int __wrap_socket(int a,int b,int c){if(sockets_failed){--sockets_failed;errno=EMFILE;return -1;}return __real_socket(a,b,c);}
extern "C" int __wrap_getaddrinfo(const char *,const char *port,const addrinfo *,addrinfo **out){
  *out=nullptr;addrinfo **next=out;
  for(int i=0;i<(dual?2:1);++i){auto *a=static_cast<addrinfo *>(std::calloc(1,sizeof(addrinfo)));assert(a);
    auto *ip=static_cast<sockaddr_in *>(std::calloc(1,sizeof(sockaddr_in)));assert(ip);
    ip->sin_family=AF_INET;ip->sin_port=htons(std::atoi(port));inet_pton(AF_INET,dual&&i==0?"127.0.0.2":"127.0.0.1",&ip->sin_addr);
    a->ai_family=AF_INET;a->ai_socktype=SOCK_STREAM;a->ai_protocol=IPPROTO_TCP;a->ai_addrlen=sizeof(*ip);a->ai_addr=reinterpret_cast<sockaddr *>(ip);*next=a;next=&a->ai_next;
  }return 0;
}
extern "C" void __wrap_freeaddrinfo(addrinfo *p){while(p){auto *next=p->ai_next;std::free(p->ai_addr);std::free(p);p=next;}}
extern "C" void aipet_wipe(void *v,size_t n){volatile unsigned char *p=static_cast<unsigned char *>(v);while(n--)*p++=0;}
static int fds(){DIR *d=opendir("/proc/self/fd");assert(d);int n=0;while(readdir(d))++n;closedir(d);return n;}
static unsigned long long now(){timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000ull+t.tv_nsec/1000000;}
static int cancel(void *){return -ECANCELED;}
int main(int argc,char **argv){assert(argc==4);const int before=fds();const std::string mode=argv[3];
  if(mode=="socket-failure"){sockets_failed=1;aipet_cloud::WebSocket s;assert(s.open("localhost","/mock",argv[1])==-EMFILE);s.close();}
  else if(mode=="refused"){aipet_cloud::WebSocket s;assert(s.open("localhost","/mock",argv[1])==-ECONNREFUSED);s.close();}
  else if(mode=="cancel"){aipet_cloud::WebSocket s(cancel);assert(s.open("localhost","/mock",argv[1])==-ECANCELED);s.close();}
  else {
    dual=mode=="tls-failover";int count=std::atoi(argv[2]);
    auto rpc=[&](int n){aipet_cloud::WebSocket s;assert(!s.open("localhost","/mock",argv[1]));s.service_budget(2000,1000);
      assert(!s.send("round-"+std::to_string(n)));std::string reply;assert(!s.receive(reply)&&reply=="{\"mock\":true}");
      if(mode=="peer-close"){assert(s.receive(reply)==-ECONNRESET);}
      auto at=now();s.close();assert(now()-at<1100);s.close();};
    // Simulate one ASR connection plus two TTS clauses per turn, in the SAME
    // process; ownership stays on one thread per complete TLS lifetime.
    for(int turn=0;turn<count;++turn){rpc(turn*3);std::thread t([&]{rpc(turn*3+1);rpc(turn*3+2);});t.join();assert(fds()==before);}
  }
  assert(fds()==before);std::printf("PASS mode=%s fd-before=%d fd-after=%d; real TLS/mock service, NOT board/account acceptance\n",mode.c_str(),before,fds());
}
