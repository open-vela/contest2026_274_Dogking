/* SPDX-License-Identifier: Apache-2.0 */
#include "cloud_voice_socket.hxx"
#include "cloud_voice_protocol.hxx"
#include <mbedtls/ssl.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/md.h>
/* NuttX sys/random.h does not declare C++ linkage guards. */
extern "C" {
#include <sys/random.h>
}
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <ctime>
#include <algorithm>
#include <atomic>
namespace aipet_cloud {
#ifndef AIPET_CLOUD_CA_FILE
#define AIPET_CLOUD_CA_FILE "/data/ai_agent/ca.pem"
#endif
static uint64_t nowms(){timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000ull+t.tv_nsec/1000000;}
static int random_bytes(void *,unsigned char *p,std::size_t n) {
  while(n){ssize_t r=getrandom(p,n,0);if(r<0&&errno==EINTR)continue;if(r<=0)return -1;p+=r;n-=r;}return 0;
}
static std::atomic<unsigned> owned_sockets{0};
struct WebSocket::State {
  mbedtls_ssl_context ssl;mbedtls_ssl_config cfg;mbedtls_x509_crt ca;mbedtls_net_context net;
  uint64_t deadline=0,idle_deadline=0,started=nowms();unsigned idle_ms=0;bool upgraded=false;
  const char *phase="opening";uint64_t received=0,sent=0;
  uint64_t wire_rx=0,wire_tx=0;char peer[INET_ADDRSTRLEN]{};
  unsigned peer_port=0,local_port=0;bool owns=false,peer_closed=false;
  int (*cancel)(void *)=nullptr;void *cookie=nullptr;
  int cancelled(){return cancel?cancel(cookie):0;}
  int failure(const char *stage,int raw,int rc,int system_error=0,int events=0) {
    std::printf("cloud-wss-v227: stage=%s phase=%s raw=%d errno=%d status=%d elapsed-ms=%llu rx=%llu tx=%llu wire-rx=%llu wire-tx=%llu peer=%s:%u local-port=%u fd=%d revents=%x owned-WSS=%u; no URL/key/body logged\n",stage,phase,raw,system_error,rc,(unsigned long long)(nowms()-started),(unsigned long long)received,(unsigned long long)sent,(unsigned long long)wire_rx,(unsigned long long)wire_tx,peer,peer_port,local_port,net.fd,events,owned_sockets.load());std::fflush(stdout);return rc;
  }
  int budget(){return nowms()>=deadline||(idle_ms&&nowms()>=idle_deadline)?failure(phase,0,-ETIMEDOUT):0;}
  void progress(){if(idle_ms)idle_deadline=nowms()+idle_ms;}
  State(){mbedtls_ssl_init(&ssl);mbedtls_ssl_config_init(&cfg);mbedtls_x509_crt_init(&ca);mbedtls_net_init(&net);}
  int release_socket() {
    int rc=0,fd=net.fd;net.fd=-1;
    // close once, never retry EINTR against a possibly reused descriptor.
    // Avoid net_free's literal shutdown(fd,2): NuttX SHUT_RDWR is 3.
    if(fd>=0&&::close(fd)<0)rc=-errno;
    if(owns){owns=false;--owned_sockets;}
    return rc;
  }
  void reset_connection() {
    (void)release_socket();mbedtls_ssl_free(&ssl);mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_free(&cfg);mbedtls_ssl_config_init(&cfg);
    upgraded=false;peer_closed=false;local_port=0;
  }
  ~State(){(void)release_socket();mbedtls_ssl_free(&ssl);mbedtls_ssl_config_free(&cfg);mbedtls_x509_crt_free(&ca);}
  static int send_wire(void *p,const unsigned char *b,std::size_t n) {
    auto &self=*static_cast<State *>(p);int r=mbedtls_net_send(&self.net,b,n);
    if(r>0){self.wire_tx+=r;self.progress();}return r;
  }
  static int recv_wire(void *p,unsigned char *b,std::size_t n) {
    auto &self=*static_cast<State *>(p);int r=mbedtls_net_recv(&self.net,b,n);
    // Progress while assembling a TLS record is still real network progress.
    if(r>0){self.wire_rx+=r;self.progress();}return r;
  }
  int wait(short events) {
    for(;;) {
      int c=cancelled();if(c)return c;if((c=budget()))return c;
      auto at=nowms(),limit=idle_ms?std::min(deadline,idle_deadline):deadline;
      int timeout=static_cast<int>(std::min<uint64_t>(100,limit>at?limit-at:0));
      pollfd p{};p.fd=net.fd;p.events=static_cast<decltype(p.events)>(events);int r=poll(&p,1,timeout);
      if(r>0) {
        if(p.revents&POLLNVAL)return failure("poll-invalid-fd",p.revents,-EBADF,EBADF,p.revents);
        if(p.revents&(POLLERR|POLLHUP)) {
          int error=0;socklen_t size=sizeof(error);
          if(getsockopt(net.fd,SOL_SOCKET,SO_ERROR,&error,&size)<0){int e=errno;return failure("poll-SO_ERROR",p.revents,-e,e,p.revents);}
          if(error)return failure("poll-SO_ERROR",p.revents,-error,error,p.revents);
          // A readable final record/EOF must be consumed even with HUP.
          if((p.revents&events)||((events&POLLIN)&&(p.revents&POLLHUP)))return 0;
          return failure("poll-no-socket-error",p.revents,-EIO,0,p.revents);
        }
        if(p.revents&events)return 0;
      }
      if(r<0&&errno!=EINTR){int e=errno;return failure("poll",r,-e,e);}
    }
  }
  int establish(const char *host,const addrinfo *address) {
    phase="TCP-socket";net.fd=socket(address->ai_family,address->ai_socktype,address->ai_protocol);
    if(net.fd<0){int e=errno;return failure(phase,-1,-e,e);}
    owns=true;++owned_sockets;
    int flags=fcntl(net.fd,F_GETFL);
    if(flags<0||fcntl(net.fd,F_SETFL,flags|O_NONBLOCK)<0){int e=errno;return failure("TCP-nonblock",-1,-e,e);}
    phase="TCP-connect";int rc=connect(net.fd,address->ai_addr,address->ai_addrlen);
    if(rc<0) {
      int e=errno;if(e!=EINPROGRESS)return failure(phase,-1,-e,e);
      rc=wait(POLLOUT);if(rc)return rc;
      int error=0;socklen_t size=sizeof(error);
      if(getsockopt(net.fd,SOL_SOCKET,SO_ERROR,&error,&size)<0){e=errno;return failure(phase,-1,-e,e);}
      if(error)return failure(phase,-1,-error,error);
    }
    sockaddr_in local{};socklen_t length=sizeof(local);
    if(!getsockname(net.fd,reinterpret_cast<sockaddr *>(&local),&length))local_port=ntohs(local.sin_port);
    std::printf("cloud-transport-v229: phase=TCP-ready peer=%s:%u local-port=%u owned-WSS=%u\n",peer,peer_port,local_port,owned_sockets.load());std::fflush(stdout);
    phase="TLS-config";
    int raw=mbedtls_ssl_config_defaults(&cfg,MBEDTLS_SSL_IS_CLIENT,MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT);
    if(raw)return failure(phase,raw,-EIO);
    mbedtls_ssl_conf_ca_chain(&cfg,&ca,nullptr);mbedtls_ssl_conf_authmode(&cfg,MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_min_tls_version(&cfg,MBEDTLS_SSL_VERSION_TLS1_2);mbedtls_ssl_conf_rng(&cfg,random_bytes,nullptr);
    raw=mbedtls_ssl_setup(&ssl,&cfg);if(raw)return failure(phase,raw,-EIO);
    raw=mbedtls_ssl_set_hostname(&ssl,host);if(raw)return failure(phase,raw,-EIO);
    mbedtls_ssl_set_bio(&ssl,this,send_wire,recv_wire,nullptr);phase="TLS-handshake";
    for(;;) {
      if((rc=cancelled())||(rc=budget()))return rc;
      raw=mbedtls_ssl_handshake(&ssl);if(!raw)break;
      if(raw!=MBEDTLS_ERR_SSL_WANT_READ&&raw!=MBEDTLS_ERR_SSL_WANT_WRITE)
        return failure(phase,raw,mbedtls_ssl_get_verify_result(&ssl)?-EACCES:raw==MBEDTLS_ERR_SSL_CONN_EOF?-ECONNRESET:-EIO);
      rc=wait(raw==MBEDTLS_ERR_SSL_WANT_READ?POLLIN:POLLOUT);if(rc)return rc;
    }
    std::printf("cloud-transport-v229: phase=TLS-ready version=%s wire-rx=%llu wire-tx=%llu\n",mbedtls_ssl_get_version(&ssl),(unsigned long long)wire_rx,(unsigned long long)wire_tx);std::fflush(stdout);return 0;
  }
  int io(unsigned char *p,std::size_t n,bool writing) {
    while(n) {
      int c=cancelled();if(c)return c;
      if((c=budget()))return c;
      int r=writing?mbedtls_ssl_write(&ssl,p,n):mbedtls_ssl_read(&ssl,p,n);
      if(r>0){if(writing)sent+=r;else received+=r;progress();n-=r;p+=r;continue;}
      if(r==MBEDTLS_ERR_SSL_WANT_READ||r==MBEDTLS_ERR_SSL_WANT_WRITE) {int q=wait(r==MBEDTLS_ERR_SSL_WANT_READ?POLLIN:POLLOUT);if(q)return q;continue;}
      return failure(writing?"TLS-write":"TLS-read",r,r==0||r==MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY?-ECONNRESET:-EIO);
    }return 0;
  }
  int frame(unsigned opcode,const unsigned char *p,std::size_t n) {
    if(n>131072)return -EMSGSIZE;
    unsigned char header[14],mask[4];if(random_bytes(nullptr,mask,4))return -EIO;
    header[0]=0x80|opcode;std::size_t len=2;
    if(n<126)header[1]=0x80|n;
    else if(n<65536){header[1]=0xfe;header[2]=n>>8;header[3]=n;len=4;}
    else {header[1]=0xff;for(unsigned i=0;i<8;i++)header[2+i]=uint64_t(n)>>(56-8*i);len=10;}
    // Coalesce header + first payload. A tiny separate TLS header record can
    // interact badly with delayed ACK/Nagle on embedded TCP peers.
    std::memcpy(header+len,mask,4);len+=4;
    unsigned char first[1038];std::memcpy(first,header,len);
    auto initial=std::min<std::size_t>(1024,n);
    for(std::size_t i=0;i<initial;i++)first[len+i]=p[i]^mask[i&3];
    int rc=io(first,len+initial,true);
    unsigned char block[1024];std::size_t pos=initial;
    while(!rc&&pos<n) {
      auto take=std::min<std::size_t>(sizeof(block),n-pos);
      for(std::size_t i=0;i<take;i++)block[i]=p[pos+i]^mask[(pos+i)&3];
      rc=io(block,take,true);pos+=take;
    }return rc;
  }
};
WebSocket::WebSocket(int (*cancel)(void *),void *cookie):s(new State){s->cancel=cancel;s->cookie=cookie;}
WebSocket::~WebSocket()=default;
int WebSocket::open(const char *host,const std::string &path,const char *port) {
  if(s->net.fd>=0)return -EALREADY;
  s->reset_connection();mbedtls_x509_crt_free(&s->ca);mbedtls_x509_crt_init(&s->ca);
  s->started=nowms();s->received=s->sent=s->wire_rx=s->wire_tx=0;s->idle_ms=0;
  s->deadline=nowms()+20000;
  s->phase="DNS";
  int cancelled=s->cancelled();if(cancelled)return cancelled;
  if(time(nullptr)<1704067200)return -EPERM;
  if(path.empty()||path[0]!='/'||path.find_first_of("\r\n")!=std::string::npos)return -EINVAL;
  // DNS uses the platform resolver's own timeout; all socket/TLS loops below
  // share a monotonic deadline. No raw credential, signed URL or body logs.
  addrinfo hints{},*list=nullptr;hints.ai_socktype=SOCK_STREAM;hints.ai_family=AF_INET;
  errno=0;int dns=getaddrinfo(host,port,&hints,&list);int dns_errno=errno;
  if(dns)return s->failure("DNS",dns,dns==EAI_MEMORY?-ENOMEM:dns==EAI_SYSTEM&&dns_errno?-dns_errno:dns==EAI_AGAIN?-EAGAIN:-EHOSTUNREACH,dns_errno);
  std::printf("cloud-wss-v227: stage=DNS-ready elapsed-ms=%llu\n",(unsigned long long)(nowms()-s->started));std::fflush(stdout);
  if(mbedtls_x509_crt_parse_file(&s->ca,AIPET_CLOUD_CA_FILE)){freeaddrinfo(list);return -EACCES;}
  const addrinfo *candidates[4]{};unsigned count=0;
  for(auto *p=list;p&&count<4;p=p->ai_next)if(p->ai_family==AF_INET&&p->ai_addr&&p->ai_addrlen>=sizeof(sockaddr_in))candidates[count++]=p;
  const uint64_t overall=s->deadline;int rc=-EHOSTUNREACH;
  for(unsigned i=0;i<count&&nowms()<overall;i++) {
    if((cancelled=s->cancelled())){rc=cancelled;break;}
    s->reset_connection();auto *ip=reinterpret_cast<const sockaddr_in *>(candidates[i]->ai_addr);
    inet_ntop(AF_INET,&ip->sin_addr,s->peer,sizeof(s->peer));s->peer_port=ntohs(ip->sin_port);
    // Fair bounded share: a dead first address cannot starve later addresses.
    const auto at=nowms();if(at>=overall){rc=-ETIMEDOUT;break;}
    s->deadline=at+(overall-at)/(count-i);
    rc=s->establish(host,candidates[i]);
    if(!rc)break;
    s->reset_connection();
    if(rc==-EACCES||rc==-ECANCELED||rc==-ENOMEM||rc==-EMFILE||rc==-ENFILE)break;
  }
  freeaddrinfo(list);s->deadline=overall;if(rc){s->reset_connection();return rc;}
  unsigned char nonce[16];if(random_bytes(nullptr,nonce,sizeof(nonce)))return -EIO;
  s->phase="HTTP-upgrade";
  std::string key=base64(nonce,sizeof(nonce));
  std::string request="GET "+path+" HTTP/1.1\r\nHost: "+host+"\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: "+key+"\r\nSec-WebSocket-Version: 13\r\n\r\n";
  rc=s->io(reinterpret_cast<unsigned char *>(&request[0]),request.size(),true);if(rc)return rc;
  std::string response;
  while(response.size()<4096&&response.find("\r\n\r\n")==std::string::npos) {unsigned char c;rc=s->io(&c,1,false);if(rc)return rc;response+=char(c);}
  if(response.find("\r\n\r\n")==std::string::npos||response.compare(0,13,"HTTP/1.1 101 ")) {
    int status=0;(void)std::sscanf(response.c_str(),"HTTP/1.1 %d",&status);
    return s->failure("HTTP-upgrade",status,status==401||status==403?-EACCES:status==429?-EAGAIN:status>=500?-EIO:-EPROTO);
  }
  std::string origin=key+"258EAFA5-E914-47DA-95CA-C5AB0DC85B11";unsigned char digest[20];
  if(mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA1),reinterpret_cast<const unsigned char *>(origin.data()),origin.size(),digest))return -EIO;
  bool accepted=false,upgrade=false,connection=false;
  std::size_t pos=response.find("\r\n")+2;
  while(pos<response.size()) {
    auto end=response.find("\r\n",pos);if(end==std::string::npos||end==pos)break;
    auto colon=response.find(':',pos);if(colon>=end)return -EPROTO;
    std::string name=response.substr(pos,colon-pos),value=response.substr(colon+1,end-colon-1);
    while(!value.empty()&&(value.front()==' '||value.front()=='\t'))value.erase(0,1);
    while(!value.empty()&&(value.back()==' '||value.back()=='\t'))value.pop_back();
    std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return std::tolower(c);});
    if(name=="sec-websocket-accept"){if(accepted||value!=base64(digest,20))return -EPROTO;accepted=true;}
    std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return std::tolower(c);});
    if(name=="upgrade")upgrade=value=="websocket";
    if(name=="connection") {
      // RFC token list, e.g. "keep-alive, Upgrade", not one exact word.
      std::size_t start=0;
      while(start<value.size()) {
        auto comma=value.find(',',start);if(comma==std::string::npos)comma=value.size();
        auto token=value.substr(start,comma-start);
        while(!token.empty()&&(token.front()==' '||token.front()=='\t'))token.erase(0,1);
        while(!token.empty()&&(token.back()==' '||token.back()=='\t'))token.pop_back();
        connection=connection||token=="upgrade";start=comma+1;
      }
    }
    pos=end+2;
  }
  if(!accepted||!upgrade||!connection)return -EPROTO;
  s->upgraded=true;
  std::printf("cloud-wss-v227: stage=UPGRADED elapsed-ms=%llu\n",(unsigned long long)(nowms()-s->started));std::fflush(stdout);return 0;
}
void WebSocket::service_budget(unsigned total_ms,unsigned idle_ms){
  s->phase="service-audio";s->deadline=nowms()+std::min(total_ms,60000u);
  s->idle_ms=std::min(idle_ms,20000u);s->progress();
}
int WebSocket::send(const std::string &json){return s->frame(1,reinterpret_cast<const unsigned char *>(json.data()),json.size());}
bool WebSocket::readable(){pollfd p{s->net.fd,POLLIN,0};return mbedtls_ssl_get_bytes_avail(&s->ssl)||poll(&p,1,0)>0;}
int WebSocket::receive(std::string &json) {
  json.clear();bool started=false;
  for(unsigned frames=0;frames<512;frames++) {
    unsigned char h[2];int rc=s->io(h,2,false);if(rc)return rc;
    unsigned opcode=h[0]&15;bool fin=h[0]&128;uint64_t n=h[1]&127;
    if((h[0]&0x70)||(h[1]&128))return -EPROTO;
    if(n==126){unsigned char b[2];rc=s->io(b,2,false);if(rc)return rc;n=(unsigned(b[0])<<8)|b[1];}
    else if(n==127){unsigned char b[8];rc=s->io(b,8,false);if(rc)return rc;n=0;for(auto c:b)n=(n<<8)|c;}
    if(n>131072||((opcode&8)&&(!fin||n>125)))return -EMSGSIZE;
    if(opcode&8) {
      unsigned char b[125];rc=s->io(b,n,false);if(rc)return rc;
      if(opcode==8){s->peer_closed=true;return -ECONNRESET;}
      if(opcode==9){rc=s->frame(10,b,n);if(rc)return rc;}
      else if(opcode!=10)return -EPROTO;
      continue;
    }
    if((!started&&opcode!=1)||(started&&opcode!=0)||n>131072-json.size())return -EPROTO;
    started=true;auto old=json.size();json.resize(old+n);if(n){rc=s->io(reinterpret_cast<unsigned char *>(&json[old]),n,false);if(rc)return rc;}
    if(fin)return utf8(json)?0:-EILSEQ;
  }return -EOVERFLOW;
}
void WebSocket::close(){
  if(!s->upgraded){(void)s->release_socket();return;}
  // Complete the protocol close with an INDEPENDENT bounded budget. No
  // semantic retry and no blocking destructor on a broken connection.
  s->phase="WS-close";s->idle_ms=0;s->deadline=nowms()+500;
  // Reply once even when the peer initiated CLOSE; do not silently omit the
  // RFC6455 close response. upgraded=false below makes repeated close inert.
  unsigned char code[]={3,232};int ws=s->frame(8,code,2);
  while(!ws&&!s->peer_closed&&nowms()<s->deadline) {
    std::string ignored;int r=receive(ignored);
    if(s->peer_closed)break;
    if(r){ws=r;break;}
  }
  s->phase="TLS-close";int tls=-EAGAIN;
  while(!ws&&nowms()<s->deadline&&!s->cancelled()) {
    int r=mbedtls_ssl_close_notify(&s->ssl);if(!r){tls=0;break;}
    if(r!=MBEDTLS_ERR_SSL_WANT_READ&&r!=MBEDTLS_ERR_SSL_WANT_WRITE){tls=r;break;}
    tls=s->wait(r==MBEDTLS_ERR_SSL_WANT_READ?POLLIN:POLLOUT);if(tls)break;
  }
  int closed=s->release_socket();s->upgraded=false;
  std::printf("cloud-transport-v229: phase=closed ws=%d peer-close=%u tls=%d fd-close=%d owned-WSS=%u; completed audio is not replayed\n",ws,s->peer_closed,tls,closed,owned_sockets.load());std::fflush(stdout);
}
}
