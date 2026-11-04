/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace aipet {
constexpr const char *sd_model_default="/data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf";
constexpr const char *ufs_model_passed="/ufs/models/qwen2.5-1.5b-instruct-q4_k_m.v174.gguf";
constexpr const char *ufs_model_default="/ufs/models/qwen2.5-1.5b-instruct-q4_k_m.gguf";
constexpr const char *sd_model_mini="/data/models/MiniCPM5-1B-Q4_K_M.gguf";
constexpr const char *ufs_model_mini="/ufs/models/MiniCPM5-1B-Q4_K_M.gguf";
constexpr uint64_t mini_model_bytes=UINT64_C(688065920);
/* No I/O or inference retries here. Preserve custom model choices. Defaults
 * have a pinned size because the old canonical UFS file was a 0-byte remnant.
 * Header/size checks are NOT a replacement for copy's full SHA256 proof. */
inline int choose_local_model(const char *configured,bool ufs_ready,
  bool (*valid)(const char *,uint64_t),char *out,size_t capacity,const char **policy)
{
  if(!configured||!valid||!out||!policy)return -EINVAL;
  const char *chosen=sd_model_default;*policy="auto-SD";
  bool automatic=!*configured||!strcmp(configured,"auto")||!strcmp(configured,sd_model_default);
  if(!strcmp(configured,"sd")) {
    *policy="forced-SD";
    if(valid(sd_model_mini,mini_model_bytes)) {chosen=sd_model_mini;*policy="forced-SD-MiniCPM5";}
  }
  else if(!automatic)
    {
      if(configured[0]!='/'||strstr(configured,".."))return -EINVAL;
      if(!strncmp(configured,"/ufs/",5)&&(!ufs_ready||!valid(configured,0)))
        *policy="UFS-unavailable-SD";
      else {chosen=configured;*policy="custom";}
    }
  else if(ufs_ready && valid(ufs_model_mini,mini_model_bytes))
    {chosen=ufs_model_mini;*policy="auto-UFS-MiniCPM5";}
  else if(valid(sd_model_mini,mini_model_bytes))
    {chosen=sd_model_mini;*policy="auto-SD-MiniCPM5";}
  else if(ufs_ready)
    {
      if(valid(ufs_model_passed,UINT64_C(1117320736)))chosen=ufs_model_passed;
      else if(valid(ufs_model_default,UINT64_C(1117320736)))chosen=ufs_model_default;
      if(chosen!=sd_model_default)*policy="auto-UFS";
    }
  if(strlen(chosen)>=capacity)return -ENAMETOOLONG;
  memcpy(out,chosen,strlen(chosen)+1);return 0;
}
}
