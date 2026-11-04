/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <cstddef>
#include <ctime>
#include <string>
#include <vector>
namespace aipet_cloud {
std::string base64(const void *,std::size_t);
int unbase64(const char *,unsigned char *,std::size_t,std::size_t &);
bool utf8(const std::string &);
std::string xfyun_url(const char *host,const char *path,const char *key,const char *secret,time_t);
std::string tencent_auth(const char *host,const char *service,const char *id,const char *secret,const std::string &body,time_t);
/* WebSocket JSON messages, not raw frames. Entire reply committed only on status=2. */
struct XfyunResult {
  std::string parts[256];bool final=false;std::size_t total=0;
  int accept(const std::string &json,bool asr,std::vector<unsigned char> &pcm,std::size_t cap);
  int text(char *,std::size_t)const;
};
int tencent_result(const std::string &,bool asr,char *,std::size_t,unsigned char *,std::size_t,std::size_t *);
int edge_result(const std::string &,unsigned char *,std::size_t,std::size_t *);
}
