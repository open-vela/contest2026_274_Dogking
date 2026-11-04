/* SPDX-License-Identifier: Apache-2.0 */
#include "cloud_voice.h"
#include "cloud_voice_protocol.hxx"
#include "cloud_voice_socket.hxx"
#include "agent_secret.h"
#include "infra/vela_tls.h"
#include "voice/voice_asr.h"
#include "voice/voice_tts.h"
#include <cJSON.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <pthread.h>
#include <unistd.h>
namespace {
using namespace aipet_cloud;
static pthread_mutex_t io_lock=PTHREAD_MUTEX_INITIALIZER;
struct Secret {
  char value[256]{};
  ~Secret(){aipet_wipe(value,sizeof(value));}
  bool load(const char *key,bool sealed=true){
    if(aipet_cloud_get(key,value,sizeof(value),sealed)||!value[0])return false;
    for(const unsigned char *p=reinterpret_cast<unsigned char *>(value);*p;p++)if(*p<33||*p>126)return false;
    return true;
  }
};
static std::string setting(const char *key,const char *fallback) {
  char value[256]{};return aipet_cloud_get(key,value,sizeof(value),0)||!value[0]?fallback:value;
}
static std::string provider(bool asr){return setting(asr?"cloud_asr_provider":"cloud_tts_provider","volc");}
static bool configured(const std::string &p,bool asr) {
  Secret a,b,c;
  if(p=="volc")return asr?(a.load("volc_appkey",false)&&b.load("volc_token",false)):a.load("volc_api_key",false);
  if(p=="xfyun")return a.load("xfyun_appid",false)&&b.load("xfyun_api_key")&&c.load("xfyun_api_secret");
  if(p=="tencent")return a.load("tencent_secret_id")&&b.load("tencent_secret_key");
  if(p=="edge"&&!asr)return !setting("edge_gateway_host","").empty()&&a.load("edge_gateway_token");
  return false;
}
using Json=std::unique_ptr<cJSON,decltype(&cJSON_Delete)>;
struct Request {
  Json root{cJSON_CreateObject(),cJSON_Delete};bool ok=true;
  cJSON *object(cJSON *p,const char *k){auto *v=cJSON_AddObjectToObject(p,k);ok=ok&&v;return v;}
  void number(cJSON *p,const char *k,double n){ok=ok&&cJSON_AddNumberToObject(p,k,n);}
  void string(cJSON *p,const char *k,const std::string &v){ok=ok&&cJSON_AddStringToObject(p,k,v.c_str());}
  std::string finish(){if(!ok||!root)return {};char *p=cJSON_PrintUnformatted(root.get());if(!p)return {};std::string s(p);cJSON_free(p);return s;}
};
static int https(const char *host,const char *port,const char *path,const vela_header_t *headers,const std::string &body,std::size_t cap,std::string &out) {
  if(time(nullptr)<1704067200)return -EPERM;
  std::unique_ptr<char[]> b(new(std::nothrow) char[cap]);if(!b)return -ENOMEM;
  std::size_t n=0;int r=vela_https_request(host,port,"POST",path,headers,body.data(),body.size(),b.get(),cap,&n);
  if(r!=200)return r==401||r==403?-EACCES:r==429?-EAGAIN:r<0?-EIO:-EREMOTEIO;
  if(!n||n>=cap-1)return -EMSGSIZE;
  out.assign(b.get(),n);return 0;
}
static int tencent(bool asr,const unsigned char *input,std::size_t bytes,const char *text,char *out,std::size_t tc,unsigned char *pcm,std::size_t pc,std::size_t *pn) {
  Secret id,key,token;if(!id.load("tencent_secret_id")||!key.load("tencent_secret_key"))return -ENOENT;
  Request req;auto *r=req.root.get();
  const char *host=asr?"asr.tencentcloudapi.com":"tts.tencentcloudapi.com";
  const char *action=asr?"SentenceRecognition":"TextToVoice";
  const char *version=asr?"2019-06-14":"2019-08-23";
  if(asr) {
    req.string(r,"EngSerViceType",setting("tencent_asr_engine","16k_zh"));req.number(r,"SourceType",1);
    req.string(r,"VoiceFormat","pcm");req.string(r,"Data",base64(input,bytes));req.number(r,"DataLen",bytes);
  }else {
    // Tencent's short-text API: <=150 Han /500 English. This bounded product
    // clause cap is stricter for mixed text; reject, never cut a spoken word.
    unsigned points=0;for(const unsigned char *p=reinterpret_cast<const unsigned char *>(text);*p;p++)if((*p&0xc0)!=0x80)++points;
    if(points>150)return -EMSGSIZE;
    static std::atomic<unsigned> serial{0};
    req.string(r,"Text",text);req.string(r,"SessionId",std::to_string(static_cast<long long>(time(nullptr)))+"-"+std::to_string(getpid())+"-"+std::to_string(++serial));
    auto voice=setting("tencent_voice_type","1001");char *end=nullptr;long v=strtol(voice.c_str(),&end,10);
    if(!end||*end||v<0||v>2147483647)return -EINVAL;
    req.number(r,"VoiceType",v);req.number(r,"ModelType",1);req.number(r,"SampleRate",16000);req.string(r,"Codec","pcm");
  }
  std::string body=req.finish();if(body.empty())return -ENOMEM;
  auto now=time(nullptr);std::string auth=tencent_auth(host,asr?"asr":"tts",id.value,key.value,body,now),stamp=std::to_string(static_cast<long long>(now));
  if(auth.empty())return -EIO;
  vela_header_t headers[]={{"Content-Type","application/json; charset=utf-8"},{"Authorization",auth.c_str()},
    {"X-TC-Action",action},{"X-TC-Version",version},{"X-TC-Timestamp",stamp.c_str()},{nullptr,nullptr},{nullptr,nullptr}};
  if(token.load("tencent_session_token"))headers[5]={"X-TC-Token",token.value};
  std::string result;int rc=https(host,"443","/",headers,body,asr?32768:4*((pc+2)/3)+4096,result);
  if(!rc)rc=tencent_result(result,asr,out,tc,pcm,pc,pn);
  aipet_wipe(&auth[0],auth.size());return rc;
}
static int xfyun(bool asr,const unsigned char *input,std::size_t bytes,const char *text,char *out,std::size_t tc,unsigned char *pcm,std::size_t pc,std::size_t *pn,aipet_cloud_cancel_fn cancel,void *cookie) {
  Secret app,key,secret;if(!app.load("xfyun_appid",false)||!key.load("xfyun_api_key")||!secret.load("xfyun_api_secret"))return -ENOENT;
  const char *host=asr?"iat-api.xfyun.cn":"tts-api.xfyun.cn",*path=asr?"/v2/iat":"/v2/tts";
  std::string url=xfyun_url(host,path,key.value,secret.value,time(nullptr));if(url.empty())return -EIO;
  WebSocket socket(cancel,cookie);int rc=socket.open(host,url);aipet_wipe(&url[0],url.size());if(rc)return rc;
  socket.service_budget(asr?40000:30000,8000);
  auto result=std::unique_ptr<XfyunResult>(new(std::nothrow) XfyunResult);if(!result)return -ENOMEM;
  std::vector<unsigned char> audio;
  unsigned messages=0;bool first_audio=false;
  auto read=[&]() {std::string json;int r=socket.receive(json);if(r)return r;
    if(++messages>512)return -EOVERFLOW;
    r=result->accept(json,asr,audio,pc);
    if(!asr&&!first_audio&&!audio.empty()) {first_audio=true;std::printf("cloud-xfyun-v227: stage=first-audio bytes=%zu\n",audio.size());std::fflush(stdout);}
    return r;};
  if(asr) {
    // 40ms PCM chunks, and drain interleaved responses on the SAME TLS owner.
    // No wpgs request (normal append results); parser also safely handles rpl.
    for(std::size_t pos=0;pos<bytes&&!rc&&!result->final;) {
      if(cancel&&(rc=cancel(cookie)))break;
      Request req;auto *root=req.root.get();
      if(!pos) {
        auto *common=req.object(root,"common");req.string(common,"app_id",app.value);
        auto *business=req.object(root,"business");req.string(business,"language","zh_cn");req.string(business,"domain","iat");req.string(business,"accent","mandarin");req.number(business,"eos",10000);
      }
      auto n=std::min<std::size_t>(1280,bytes-pos);auto *data=req.object(root,"data");
      req.number(data,"status",pos?1:0);req.string(data,"format","audio/L16;rate=16000");req.string(data,"encoding","raw");req.string(data,"audio",base64(input+pos,n));
      auto json=req.finish();rc=json.empty()?-ENOMEM:socket.send(json);pos+=n;
      if(!rc){usleep(40000);if(socket.readable())rc=read();}
      // A server endpoint before all supplied audio is a failure, not a silently
      // truncated successful transcript that could dispatch an unintended action.
      if(!rc&&result->final&&pos<bytes)rc=-EPROTO;
    }
    if(!rc&&!result->final)rc=socket.send("{\"data\":{\"status\":2,\"format\":\"audio/L16;rate=16000\",\"encoding\":\"raw\",\"audio\":\"\"}}");
  }else {
    Request req;auto *root=req.root.get(),*common=req.object(root,"common"),*business=req.object(root,"business"),*data=req.object(root,"data");
    req.string(common,"app_id",app.value);req.string(business,"aue","raw");req.string(business,"auf","audio/L16;rate=16000");
    req.string(business,"vcn",setting("xfyun_voice","xiaoyan"));req.string(business,"tte","UTF8");req.string(business,"reg","2");
    req.number(data,"status",2);req.string(data,"text",base64(text,std::strlen(text)));
    auto json=req.finish();rc=json.empty()?-ENOMEM:socket.send(json);
    std::printf("cloud-xfyun-v227: service=TTS stage=request-sent status=%d text-bytes=%zu\n",rc,std::strlen(text));std::fflush(stdout);
  }
  while(!rc&&!result->final)rc=read();
  if(!rc) {
    if(asr)rc=result->text(out,tc);
    else if(audio.empty()||(audio.size()&1))rc=-EPROTO;
    else {std::memcpy(pcm,audio.data(),audio.size());*pn=audio.size();}
  }
  if(!rc)socket.close();
  return rc;
}
static int edge(const char *text,unsigned char *pcm,std::size_t cap,std::size_t *n) {
  Secret token;if(!token.load("edge_gateway_token"))return -ENOENT;
  const auto host=setting("edge_gateway_host","");auto port=setting("edge_gateway_port","443");
  if(host.empty()||host.size()>253||host.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-")!=std::string::npos)return -EINVAL;
  if(port.empty()||port.find_first_not_of("0123456789")!=std::string::npos||std::strtoul(port.c_str(),nullptr,10)<1||std::strtoul(port.c_str(),nullptr,10)>65535)return -EINVAL;
  Request req;req.string(req.root.get(),"text",text);req.string(req.root.get(),"voice",setting("edge_voice","zh-CN-XiaoxiaoNeural"));auto body=req.finish();if(body.empty())return -ENOMEM;
  std::string auth="Bearer "+std::string(token.value),result;
  vela_header_t headers[]={{"Content-Type","application/json; charset=utf-8"},{"Authorization",auth.c_str()},{nullptr,nullptr}};
  int rc=https(host.c_str(),port.c_str(),"/v1/tts",headers,body,4*((cap+2)/3)+4096,result);
  if(!rc)rc=edge_result(result,pcm,cap,n);
  aipet_wipe(&auth[0],auth.size());return rc;
}
}
extern "C" int aipet_cloud_provider_valid(const char *kind,const char *name) {
  if(!kind||!name||(!std::strcmp(kind,"asr")&&!std::strcmp(name,"edge")))return 0;
  if(std::strcmp(kind,"asr")&&std::strcmp(kind,"tts"))return 0;
  return !std::strcmp(name,"volc")||!std::strcmp(name,"xfyun")||!std::strcmp(name,"tencent")||!std::strcmp(name,"edge");
}
extern "C" int aipet_cloud_ready(void){return configured(provider(true),true)&&configured(provider(false),false);}
extern "C" void aipet_cloud_status(void) {
  auto a=provider(true),t=provider(false);
  // Do not print arbitrary unvalidated config values as terminal commands.
  std::printf("cloud-voice-v218: ASR=%s credentials=%s; TTS=%s credentials=%s; credentials != API/board acceptance\n",
    aipet_cloud_provider_valid("asr",a.c_str())?a.c_str():"invalid",configured(a,true)?"present":"missing",
    aipet_cloud_provider_valid("tts",t.c_str())?t.c_str():"invalid",configured(t,false)?"present":"missing");
  std::puts("choices: ASR=volc|xfyun|tencent; TTS=volc|xfyun|tencent|edge (experimental HTTPS companion gateway, TTS only)");
}
extern "C" int aipet_cloud_asr(const unsigned char *pcm,std::size_t n,char *out,std::size_t cap) {
  return aipet_cloud_asr_cancel(pcm,n,out,cap,nullptr,nullptr);
}
extern "C" int aipet_cloud_asr_cancel(const unsigned char *pcm,std::size_t n,char *out,std::size_t cap,aipet_cloud_cancel_fn cancel,void *cookie) {
  if(!pcm||!n||n>256000||(n&1)||!out||cap<2||cap>4097)return -EINVAL;
  out[0]=0;
  if(cancel){int c=cancel(cookie);if(c)return c;}
  if(pthread_mutex_trylock(&io_lock))return -EBUSY;
  struct Unlock{~Unlock(){pthread_mutex_unlock(&io_lock);}} unlock;
  auto p=provider(true);int rc=-EINVAL;
  if(!configured(p,true))return -ENOENT;
  if(p=="volc")rc=voice_asr_recognize(pcm,n,out,cap);
  else if(p=="tencent")rc=tencent(true,pcm,n,nullptr,out,cap,nullptr,0,nullptr);
  else if(p=="xfyun")rc=xfyun(true,pcm,n,nullptr,out,cap,nullptr,0,nullptr,cancel,cookie);
  if(cancel){int c=cancel(cookie);if(c)rc=c;}
  if(rc)out[0]=0;
  std::printf("cloud-ASR: provider=%s status=%d; no semantic retry\n",p.c_str(),rc);return rc;
}
extern "C" int aipet_cloud_tts(const char *text,unsigned char *pcm,std::size_t cap,std::size_t *n) {
  return aipet_cloud_tts_cancel(text,pcm,cap,n,nullptr,nullptr);
}
extern "C" int aipet_cloud_tts_cancel(const char *text,unsigned char *pcm,std::size_t cap,std::size_t *n,aipet_cloud_cancel_fn cancel,void *cookie) {
  if(n)*n=0;
  if(cancel){int c=cancel(cookie);if(c)return c;}
  if(!text||!pcm||!n||!cap||cap>320000)return -EINVAL;
  std::string s(text);if(s.empty()||s.size()>4096||!utf8(s))return -EILSEQ;
  if(pthread_mutex_trylock(&io_lock))return -EBUSY;
  struct Unlock{~Unlock(){pthread_mutex_unlock(&io_lock);}} unlock;
  auto p=provider(false);int rc=-EINVAL;if(!configured(p,false))return -ENOENT;
  if(p=="volc")rc=voice_tts_speak(text,pcm,cap,n);
  else if(p=="tencent")rc=tencent(false,nullptr,0,text,nullptr,0,pcm,cap,n);
  else if(p=="xfyun")rc=xfyun(false,nullptr,0,text,nullptr,0,pcm,cap,n,cancel,cookie);
  else if(p=="edge")rc=edge(text,pcm,cap,n);
  if(cancel){int c=cancel(cookie);if(c)rc=c;}
  if(!rc&&(!*n||*n>cap||(*n&1)))rc=-EPROTO;
  if(rc){*n=0;aipet_wipe(pcm,cap);}
  std::printf("cloud-TTS: provider=%s status=%d PCM=16k/mono/S16LE; no local substitution\n",p.c_str(),rc);return rc;
}
