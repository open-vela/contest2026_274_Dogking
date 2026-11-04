/* SPDX-License-Identifier: Apache-2.0 */
#include "cloud_voice_protocol.hxx"
#include "agent_secret.h"
#include <cJSON.h>
#include <mbedtls/md.h>
#include <mbedtls/base64.h>
#include <cerrno>
#include <cstring>
#include <memory>
#include <algorithm>
#include <cstdio>
namespace aipet_cloud {
using Json=std::unique_ptr<cJSON,decltype(&cJSON_Delete)>;
static Json parse(const std::string &s) {
  if(s.empty()||s.find('\0')!=std::string::npos||s.find("\\u0000")!=std::string::npos)return Json(nullptr,cJSON_Delete);
  return Json(cJSON_ParseWithOpts(s.c_str(),nullptr,1),cJSON_Delete);
}
static cJSON *get(cJSON *j,const char *s){return cJSON_GetObjectItemCaseSensitive(j,s);}
static const char *str(cJSON *j){return cJSON_IsString(j)?j->valuestring:nullptr;}
static bool integer(cJSON *j,int lo,int hi){return cJSON_IsNumber(j)&&j->valuedouble>=lo&&j->valuedouble<=hi&&j->valuedouble==j->valueint;}
std::string base64(const void *p,std::size_t n) {
  std::string out(4*((n+2)/3)+1,'\0');std::size_t used=0;
  if(mbedtls_base64_encode(reinterpret_cast<unsigned char *>(&out[0]),out.size(),&used,static_cast<const unsigned char *>(p),n))return {};
  out.resize(used);return out;
}
int unbase64(const char *s,unsigned char *out,std::size_t cap,std::size_t &used) {
  used=0;if(!s)return -EPROTO;
  std::size_t n=std::strlen(s);
  if(n%4||n>4*((cap+2)/3))return -EMSGSIZE;
  for(std::size_t i=0;i<n;i++) {
    unsigned char c=s[i];
    if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='+'||c=='/'||c=='='))return -EPROTO;
    if(c=='='&&(i<n-2||(i==n-2&&s[n-1]!='=')))return -EPROTO;
  }
  return mbedtls_base64_decode(out,cap,&used,reinterpret_cast<const unsigned char *>(s),n)?-EPROTO:0;
}
bool utf8(const std::string &s) {
  for(std::size_t i=0;i<s.size();) {
    unsigned char a=s[i++];if(!a)return false;if(a<128)continue;
    unsigned n;unsigned cp,min;
    if(a>=0xc2&&a<=0xdf){n=1;cp=a&31;min=128;}
    else if(a>=0xe0&&a<=0xef){n=2;cp=a&15;min=2048;}
    else if(a>=0xf0&&a<=0xf4){n=3;cp=a&7;min=65536;}
    else return false;
    if(n>s.size()-i)return false;
    while(n--){unsigned char c=s[i++];if((c&0xc0)!=0x80)return false;cp=(cp<<6)|(c&63);}
    if(cp<min||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))return false;
  }return true;
}
static void hmac(const void *k,std::size_t nk,const std::string &s,unsigned char *out) {
  if(mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),static_cast<const unsigned char *>(k),nk,reinterpret_cast<const unsigned char *>(s.data()),s.size(),out))std::memset(out,0,32);
}
static std::string hex(const unsigned char *p,std::size_t n) {
  const char *digits="0123456789abcdef";std::string out;out.reserve(2*n);
  while(n--){out+=digits[*p>>4];out+=digits[*p++&15];}return out;
}
static std::string sha(const std::string &s) {
  unsigned char b[32]{};
  if(mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),reinterpret_cast<const unsigned char *>(s.data()),s.size(),b))return {};
  return hex(b,32);
}
static std::string escaped(const std::string &s) {
  const char *d="0123456789ABCDEF";std::string out;
  for(unsigned char c:s) {
    if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')out+=c;
    else {out+='%';out+=d[c>>4];out+=d[c&15];}
  }return out;
}
std::string xfyun_url(const char *host,const char *path,const char *key,const char *secret,time_t now) {
  tm t{};if(!gmtime_r(&now,&t))return {};
  // RFC1123, independent of locale/strftime weekday translations.
  static const char *days[]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  static const char *months[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
  char date[40];std::snprintf(date,sizeof(date),"%s, %02d %s %04d %02d:%02d:%02d GMT",days[t.tm_wday],t.tm_mday,months[t.tm_mon],1900+t.tm_year,t.tm_hour,t.tm_min,t.tm_sec);
  std::string source=std::string("host: ")+host+"\ndate: "+date+"\nGET "+path+" HTTP/1.1";
  unsigned char b[32];hmac(secret,std::strlen(secret),source,b);
  std::string auth=std::string("api_key=\"")+key+"\", algorithm=\"hmac-sha256\", headers=\"host date request-line\", signature=\""+base64(b,32)+"\"";
  std::string out=std::string(path)+"?authorization="+escaped(base64(auth.data(),auth.size()))+"&date="+escaped(date)+"&host="+escaped(host);
  aipet_wipe(&auth[0],auth.size());aipet_wipe(b,sizeof(b));return out;
}
std::string tencent_auth(const char *host,const char *service,const char *id,const char *secret,const std::string &body,time_t now) {
  tm t{};if(!gmtime_r(&now,&t))return {};char date[16];std::strftime(date,sizeof(date),"%Y-%m-%d",&t);
  const std::string headers="content-type;host";
  std::string canonical=std::string("POST\n/\n\ncontent-type:application/json; charset=utf-8\nhost:")+host+"\n\n"+headers+"\n"+sha(body);
  std::string scope=std::string(date)+"/"+service+"/tc3_request";
  std::string source="TC3-HMAC-SHA256\n"+std::to_string(static_cast<long long>(now))+"\n"+scope+"\n"+sha(canonical);
  std::string key="TC3"+std::string(secret);unsigned char a[32],b[32];
  hmac(key.data(),key.size(),date,a);hmac(a,32,service,b);hmac(b,32,"tc3_request",a);hmac(a,32,source,b);
  std::string auth="TC3-HMAC-SHA256 Credential="+std::string(id)+"/"+scope+", SignedHeaders="+headers+", Signature="+hex(b,32);
  aipet_wipe(&key[0],key.size());aipet_wipe(a,32);aipet_wipe(b,32);return auth;
}
int XfyunResult::accept(const std::string &s,bool asr,std::vector<unsigned char> &pcm,std::size_t cap) {
  auto root=parse(s);if(!root)return -EPROTO;
  cJSON *code=get(root.get(),"code"),*data=get(root.get(),"data");
  if(!integer(code,0,0)) {
    if(cJSON_IsNumber(code))std::printf("cloud-xfyun-v226: service=%s remote-code=%.0f; no message/credentials/audio logged\n",asr?"ASR":"TTS",code->valuedouble);
    return cJSON_IsNumber(code)?-EREMOTEIO:-EPROTO;
  }
  if(final)return -EPROTO;
  // Documented Xfyun TTS keepalive: code=0 and data=null/absent is NOT a
  // terminal result or an error. Keep waiting for a real status=2 audio event.
  if(!asr&&(!data||cJSON_IsNull(data)))return 0;
  cJSON *status=get(data,"status");if(!integer(status,0,2)||final)return -EPROTO;
  if(asr) {
    cJSON *r=get(data,"result");
    if(r) {
      auto sn=get(r,"sn");auto ws=get(r,"ws");if(!integer(sn,0,255)||!cJSON_IsArray(ws))return -EPROTO;
      std::string part;auto *word=static_cast<cJSON *>(nullptr);
      cJSON_ArrayForEach(word,ws) {
        const char *w=str(get(cJSON_GetArrayItem(get(word,"cw"),0),"w"));
        if(!w||part.size()+std::strlen(w)>4096)return -EPROTO;
        part+=w;
      }
      if(!utf8(part))return -EILSEQ;
      const char *pgs=str(get(r,"pgs"));
      if(pgs&&std::strcmp(pgs,"apd")&&std::strcmp(pgs,"rpl"))return -EPROTO;
      if(pgs&&!std::strcmp(pgs,"rpl")) {
        auto *rg=get(r,"rg");auto *lo=cJSON_GetArrayItem(rg,0),*hi=cJSON_GetArrayItem(rg,1);
        if(!cJSON_IsArray(rg)||cJSON_GetArraySize(rg)!=2||!integer(lo,0,255)||!integer(hi,lo->valueint,255))return -EPROTO;
        for(int i=lo->valueint;i<=hi->valueint;i++){total-=parts[i].size();parts[i].clear();}
      }
      total-=parts[sn->valueint].size();parts[sn->valueint]=part;total+=part.size();if(total>4096)return -EMSGSIZE;
    }
  } else {
    const char *audio=str(get(data,"audio"));if(!audio)return -EPROTO;
    std::vector<unsigned char> decoded(std::min<std::size_t>(cap,3*(std::strlen(audio)/4)+3));std::size_t n=0;
    int rc=unbase64(audio,decoded.data(),decoded.size(),n);if(rc)return rc;
    if(pcm.size()>cap||n>cap-pcm.size())return -EMSGSIZE;
    pcm.insert(pcm.end(),decoded.begin(),decoded.begin()+n);
  }
  final=status->valueint==2;return 0;
}
int XfyunResult::text(char *out,std::size_t cap)const {
  if(!final)return -EAGAIN;
  if(!total)return -ENODATA;
  if(total>=cap)return -EMSGSIZE;
  std::size_t n=0;for(const auto &p:parts){std::memcpy(out+n,p.data(),p.size());n+=p.size();}out[n]=0;return 0;
}
int tencent_result(const std::string &s,bool asr,char *text,std::size_t tc,unsigned char *pcm,std::size_t pc,std::size_t *pn) {
  auto root=parse(s);if(!root)return -EPROTO;auto *response=get(root.get(),"Response");
  if(!cJSON_IsObject(response))return -EPROTO;
  if(get(response,"Error"))return -EREMOTEIO;
  if(asr) {
    const char *v=str(get(response,"Result"));if(!v)return -EPROTO;
    std::string value(v);if(!utf8(value))return -EILSEQ;if(value.empty())return -ENODATA;
    if(value.size()>=tc)return -EMSGSIZE;
    std::memcpy(text,v,value.size()+1);return 0;
  }
  std::size_t used=0;int rc=unbase64(str(get(response,"Audio")),pcm,pc,used);
  if(!rc&&(!used||(used&1)))rc=-EPROTO;
  if(!rc)*pn=used;
  return rc;
}
int edge_result(const std::string &s,unsigned char *pcm,std::size_t cap,std::size_t *n) {
  auto root=parse(s);if(!root||!integer(get(root.get(),"sample_rate"),16000,16000)||
      !integer(get(root.get(),"channels"),1,1)||!integer(get(root.get(),"bits"),16,16))return -EPROTO;
  std::size_t used=0;int rc=unbase64(str(get(root.get(),"pcm")),pcm,cap,used);
  if(!rc&&(!used||(used&1)))rc=-EPROTO;
  if(!rc)*n=used;
  return rc;
}
}
