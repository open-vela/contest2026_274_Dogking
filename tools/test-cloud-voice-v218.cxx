/* SPDX-License-Identifier: Apache-2.0 */
/* Actual provider/protocol/crypto code, MOCK HTTPS/WebSocket, no paid calls. */
#include "cloud_voice.h"
#include "cloud_voice_protocol.hxx"
#include "cloud_voice_socket.hxx"
#include "infra/vela_tls.h"
#include "voice/voice_asr.h"
#include "voice/voice_tts.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <deque>
#include <iostream>
#include <cerrno>
static std::map<std::string,std::string> cfg;
static unsigned http_calls,ws_calls,volc_calls;
static int http_status=200;
static std::string http_reply,last_host,last_body,ws_host;
static std::map<std::string,std::string> headers;
static std::deque<std::string> incoming;
extern "C" void aipet_wipe(void *v,size_t n){volatile unsigned char *p=static_cast<unsigned char *>(v);while(n--)*p++=0;}
extern "C" int aipet_cloud_get(const char *key,char *out,size_t cap,int){auto it=cfg.find(key);if(it==cfg.end()||it->second.size()>=cap)return -ENOENT;std::memcpy(out,it->second.c_str(),it->second.size()+1);return 0;}
extern "C" int vela_https_request(const char *host,const char *port,const char *method,const char *path,const vela_header_t *h,const char *body,size_t bytes,char *out,size_t cap,size_t *n) {
  ++http_calls;assert(!std::strcmp(port,"443")||!std::strcmp(port,"8443"));assert(!std::strcmp(method,"POST"));
  assert(!std::strcmp(path,"/")||!std::strcmp(path,"/v1/tts"));
  last_host=host;last_body.assign(body,bytes);headers.clear();for(;h->name;h++)headers[h->name]=h->value;
  assert(headers["Content-Type"]=="application/json; charset=utf-8");
  if(last_host.find("tencentcloudapi")!=std::string::npos) {
    auto action=last_host[0]=='a'?"SentenceRecognition":"TextToVoice";
    assert(headers["X-TC-Action"]==action);
    assert(headers["Authorization"]==aipet_cloud::tencent_auth(host,last_host[0]=='a'?"asr":"tts","ID","SECRET",last_body,std::stoll(headers["X-TC-Timestamp"])));
  }
  *n=std::min(http_reply.size(),cap-1);std::memcpy(out,http_reply.data(),*n);out[*n]=0;return http_status;
}
extern "C" int voice_asr_recognize(const unsigned char *,size_t,char *out,size_t){++volc_calls;std::strcpy(out,"火山");return 0;}
extern "C" int voice_tts_speak(const char *,unsigned char *out,size_t,size_t *n){++volc_calls;out[0]=1;out[1]=0;*n=2;return 0;}
namespace aipet_cloud {
struct WebSocket::State {};
WebSocket::WebSocket(int (*)(void *),void *):s(new State){}
WebSocket::~WebSocket()=default;
int WebSocket::open(const char *host,const std::string &url,const char *port){++ws_calls;ws_host=host;assert(!std::strcmp(port,"443")&&url.find("authorization=")!=std::string::npos);return 0;}
void WebSocket::service_budget(unsigned total,unsigned idle){assert(total>=30000&&total<=40000&&idle==8000);}
int WebSocket::send(const std::string &json) {
  if(ws_host=="tts-api.xfyun.cn") {
    assert(json.find("audio/L16;rate=16000")!=std::string::npos&&json.find("UTF8")!=std::string::npos);
    assert(json.find("\"aue\":\"raw\"")!=std::string::npos&&json.find("\"vcn\":\"xiaoyan\"")!=std::string::npos&&json.find("\"status\":2")!=std::string::npos);
    incoming.push_back("{\"code\":0,\"data\":null}");
    incoming.push_back("{\"code\":0,\"data\":{\"status\":1,\"audio\":\"AQI=\"}}");
    incoming.push_back("{\"code\":0,\"data\":{\"status\":2,\"audio\":\"AwQ=\"}}");
  }else if(json.find("\"status\":2")!=std::string::npos) {
    incoming.push_back("{\"code\":0,\"data\":{\"status\":2,\"result\":{\"sn\":0,\"ws\":[{\"cw\":[{\"w\":\"你好\"}]}]}}}");
  }
  return 0;
}
int WebSocket::receive(std::string &json){if(incoming.empty())return -ETIMEDOUT;json=incoming.front();incoming.pop_front();return 0;}
bool WebSocket::readable(){return !incoming.empty();}
void WebSocket::close(){}
}
int main(int argc,char **argv) {
  using namespace aipet_cloud;
  if(argc==2&&std::string(argv[1])=="signatures") {
    std::cout<<tencent_auth("asr.tencentcloudapi.com","asr","ID","SECRET","{\"DataLen\":4}",1551113065)<<'\n';
    std::cout<<xfyun_url("iat-api.xfyun.cn","/v2/iat","KEY","SECRET",1562744143)<<'\n';
    std::cout<<xfyun_url("tts-api.xfyun.cn","/v2/tts","KEY","SECRET",1562744143)<<'\n';return 0;
  }
  assert(!aipet_cloud_ready());assert(!aipet_cloud_provider_valid("asr","edge"));assert(!aipet_cloud_provider_valid("x","tencent"));
  assert(aipet_cloud_provider_valid("tts","edge"));
  unsigned char pcm[320000]{};size_t n=0;char text[4097]{};
  auto cancelled=[](void *)->int{return -ECANCELED;};
  assert(aipet_cloud_asr_cancel(pcm,3200,text,sizeof(text),cancelled,nullptr)==-ECANCELED);
  n=123;assert(aipet_cloud_tts_cancel("取消前不得联网",pcm,sizeof(pcm),&n,cancelled,nullptr)==-ECANCELED&&n==0);
  assert(http_calls==0&&ws_calls==0&&volc_calls==0);
  assert(aipet_cloud_asr(pcm,4,text,sizeof(text))==-ENOENT&&!http_calls&&!ws_calls);
  assert(aipet_cloud_tts("你好",pcm,sizeof(pcm),&n)==-ENOENT&&!n);
  cfg={{"volc_appkey","APP"},{"volc_token","TOKEN"},{"volc_api_key","API"}};
  assert(aipet_cloud_ready());assert(!aipet_cloud_asr(pcm,4,text,sizeof(text))&&!std::strcmp(text,"火山"));
  assert(!aipet_cloud_tts("你好",pcm,sizeof(pcm),&n)&&n==2&&volc_calls==2);
  cfg={{"cloud_asr_provider","tencent"},{"cloud_tts_provider","tencent"},{"tencent_secret_id","ID"},{"tencent_secret_key","SECRET"}};
  assert(aipet_cloud_ready());http_reply="{\"Response\":{\"Result\":\"你好 Edge\"}}";
  assert(!aipet_cloud_asr(pcm,4,text,sizeof(text))&&!std::strcmp(text,"你好 Edge"));
  assert(last_body.find("\"DataLen\":4")!=std::string::npos&&last_body.find("\"SourceType\":1")!=std::string::npos&&headers.count("X-TC-Token")==0);
  cfg["tencent_session_token"]="SESSION";
  http_reply="{\"Response\":{\"Audio\":\"AQIDBA==\"}}";
  assert(!aipet_cloud_tts("你好",pcm,sizeof(pcm),&n)&&n==4&&pcm[3]==4&&headers["X-TC-Token"]=="SESSION");
  assert(last_body.find("\"Codec\":\"pcm\"")!=std::string::npos);
  for(const char *bad:{"{\"Response\":{\"Error\":{\"Code\":\"AuthFailure\"}}}","{}","{\"Response\":{\"Audio\":\"AQ==\"}}","{\"Response\":{\"Audio\":\"!!!!\"}}","{\"Response\":{\"Audio\":\"AQIDBA==\"}}x"}) {
    http_reply=bad;n=123;assert(aipet_cloud_tts("你好",pcm,sizeof(pcm),&n)<0&&n==0&&pcm[0]==0);
  }
  http_status=401;assert(aipet_cloud_tts("你好",pcm,sizeof(pcm),&n)==-EACCES);http_status=200;
  http_reply="{\"Response\":{\"Audio\":\"AQIDBA==\"}}";
  assert(aipet_cloud_tts("你好",pcm,2,&n)<0&&!n);
  assert(aipet_cloud_tts(std::string(151,'a').c_str(),pcm,sizeof(pcm),&n)==-EMSGSIZE);
  assert(aipet_cloud_tts("\xff",pcm,sizeof(pcm),&n)==-EILSEQ);
  cfg["cloud_asr_provider"]="edge";assert(!aipet_cloud_ready());
  cfg={{"cloud_asr_provider","xfyun"},{"cloud_tts_provider","xfyun"},{"xfyun_appid","APP"},{"xfyun_api_key","KEY"},{"xfyun_api_secret","SECRET"}};
  assert(aipet_cloud_ready());assert(!aipet_cloud_asr(pcm,2560,text,sizeof(text))&&!std::strcmp(text,"你好"));
  assert(!aipet_cloud_tts("Hello，中文。",pcm,sizeof(pcm),&n)&&n==4&&pcm[0]==1&&pcm[3]==4&&incoming.empty());
  {XfyunResult r;std::vector<unsigned char> audio;
    assert(!r.accept("{\"code\":0,\"data\":{\"status\":1,\"result\":{\"sn\":1,\"ws\":[{\"cw\":[{\"w\":\"错字\"}]}]}}}",true,audio,0));
    assert(!r.accept("{\"code\":0,\"data\":{\"status\":2,\"result\":{\"sn\":2,\"pgs\":\"rpl\",\"rg\":[1,1],\"ws\":[{\"cw\":[{\"w\":\"正确\"}]}]}}}",true,audio,0));
    assert(!r.text(text,sizeof(text))&&!std::strcmp(text,"正确"));
    assert(r.accept("{\"code\":0,\"data\":{\"status\":2}}",true,audio,0)==-EPROTO);
  }
  cfg={{"cloud_asr_provider","tencent"},{"cloud_tts_provider","edge"},{"tencent_secret_id","ID"},{"tencent_secret_key","SECRET"},{"edge_gateway_host","voice.example.test"},{"edge_gateway_port","8443"},{"edge_gateway_token","GATEWAY"}};
  http_reply="{\"sample_rate\":16000,\"channels\":1,\"bits\":16,\"pcm\":\"AQIDBA==\"}";
  assert(aipet_cloud_ready()&&!aipet_cloud_tts("你好",pcm,sizeof(pcm),&n)&&n==4);
  assert(last_host=="voice.example.test"&&headers["Authorization"]=="Bearer GATEWAY");
  http_reply="{\"sample_rate\":24000,\"channels\":1,\"bits\":16,\"pcm\":\"AQIDBA==\"}";
  assert(aipet_cloud_tts("你好",pcm,sizeof(pcm),&n)==-EPROTO&&!n);
  cfg["edge_gateway_host"]="http://bad/path";assert(aipet_cloud_tts("你好",pcm,sizeof(pcm),&n)==-EINVAL);
  cfg["edge_gateway_host"]="voice.example.test";cfg["edge_gateway_token"]="GATEWAY\r\nINJECT";
  assert(!aipet_cloud_ready()&&aipet_cloud_tts("你好",pcm,sizeof(pcm),&n)==-ENOENT);
  size_t got=99;assert(unbase64("AQ I=",pcm,sizeof(pcm),got)<0);assert(unbase64("A===",pcm,sizeof(pcm),got)<0);
  assert(!utf8("\xed\xa0\x80")&&!utf8("\xf4\x90\x80\x80")&&!utf8("\xc0\x80")&&utf8("中文 English 😀"));
  aipet_cloud_status();
  std::puts("PASS v218 actual providers/crypto/protocol, MOCK TLS/HTTP/WS; no cloud API, no board acceptance.");
}
