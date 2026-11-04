/* SPDX-License-Identifier: Apache-2.0 */
#include "voice_tts_frontend.hxx"
#include <cerrno>
#include <cstdio>
#include <string>
int main(int argc,char **argv)
{
  if(argc!=3)return 2;
  aipet::VoiceTtsFrontend *f=nullptr;
  int32_t rc=aipet::voice_tts_frontend_open(argv[1],argv[2],&f);
  std::fwrite(&rc,4,1,stdout);std::fflush(stdout);
  if(rc)return 2;
  uint32_t n;
  while(std::fread(&n,4,1,stdin)==1 && n) {
    if(n>4096)break;
    std::string s(n,'\0');if(std::fread(&s[0],1,n,stdin)!=n)break;
    std::vector<std::vector<int64_t>> result;
    rc=s.find('\0')==std::string::npos?aipet::voice_tts_frontend_text(f,s.c_str(),result):-EINVAL;
    std::fwrite(&rc,4,1,stdout);
    if(!rc) {
      n=result.size();std::fwrite(&n,4,1,stdout);
      for(const auto &v:result){n=v.size();std::fwrite(&n,4,1,stdout);std::fwrite(v.data(),8,n,stdout);}
    }
    std::fflush(stdout);
  }
  aipet::voice_tts_frontend_close(f);return 0;
}
