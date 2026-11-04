/* SPDX-License-Identifier: Apache-2.0 */
#include <cassert>
#include <cstdio>
#include <cstring>
#include "local_model_path.hxx"
static int files;
static bool valid(const char *p,uint64_t bytes)
{
  if(!strcmp(p,aipet::sd_model_mini))return (files&8)&&bytes==aipet::mini_model_bytes;
  if(!strcmp(p,aipet::ufs_model_mini))return (files&16)&&bytes==aipet::mini_model_bytes;
  if(!strcmp(p,aipet::ufs_model_passed))return (files&1)&&bytes==1117320736;
  if(!strcmp(p,aipet::ufs_model_default))return (files&2)&&bytes==1117320736;
  if(!strcmp(p,"/ufs/custom.gguf"))return (files&4)&&!bytes;
  return false;
}
int main()
{
  char path[1024];const char *policy;
  for(files=0;files<8;files++)for(int ready=0;ready<2;ready++)
    {
      assert(!aipet::choose_local_model("auto",ready,valid,path,sizeof(path),&policy));
      const char *wanted=!ready?aipet::sd_model_default:files&1?aipet::ufs_model_passed:
                         files&2?aipet::ufs_model_default:aipet::sd_model_default;
      assert(!strcmp(path,wanted));
      assert(!aipet::choose_local_model(aipet::sd_model_default,ready,valid,path,sizeof(path),&policy));
      assert(!strcmp(path,wanted)); /* Legacy default migrates, no config overwrite. */
      assert(!aipet::choose_local_model("sd",ready,valid,path,sizeof(path),&policy));
      assert(!strcmp(path,aipet::sd_model_default));
      assert(!aipet::choose_local_model("/data/models/custom.gguf",ready,valid,path,sizeof(path),&policy));
      assert(!strcmp(path,"/data/models/custom.gguf"));
      assert(!aipet::choose_local_model("/ufs/custom.gguf",ready,valid,path,sizeof(path),&policy));
      assert(!strcmp(path,ready&&(files&4)?"/ufs/custom.gguf":aipet::sd_model_default));
    }
  assert(aipet::choose_local_model("/ufs/../model",true,valid,path,sizeof(path),&policy)<0);
  assert(aipet::choose_local_model("auto",true,valid,path,3,&policy)<0);
  for(files=8;files<32;files++)for(int ready=0;ready<2;ready++) {
    assert(!aipet::choose_local_model("auto",ready,valid,path,sizeof(path),&policy));
    const char *wanted=ready&&(files&16)?aipet::ufs_model_mini:
      files&8?aipet::sd_model_mini:ready&&(files&1)?aipet::ufs_model_passed:
      ready&&(files&2)?aipet::ufs_model_default:aipet::sd_model_default;
    assert(!strcmp(path,wanted));
    assert(!aipet::choose_local_model("sd",ready,valid,path,sizeof(path),&policy));
    assert(!strcmp(path,files&8?aipet::sd_model_mini:aipet::sd_model_default));
    assert(!aipet::choose_local_model("/data/custom.gguf",ready,valid,path,sizeof(path),&policy));
    assert(!strcmp(path,"/data/custom.gguf"));
  }
  puts("Agent model policy PASS: auto/legacy/UFS absent or invalid/explicit SD/custom/bounds; no inference retry");
}
