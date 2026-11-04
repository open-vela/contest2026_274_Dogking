/* SPDX-License-Identifier: Apache-2.0 */
#ifndef A733_TENSOR_UFS_PATH_H
#define A733_TENSOR_UFS_PATH_H
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
/* Read-only extension for FOUR known voice packages only. Not a general
 * /ufs prefix grant. Embedded package LOAD/CRC/CID/DMA checks stay intact. */
static inline bool a733_tensor_ufs_voice_path(const char *path,size_t capacity)
{
  static const char *const files[]={
    "/ufs/npu/general-asr/asr-encoder.a7tv",
    "/ufs/npu/general-asr/asr-decoder.a7tv",
    "/ufs/npu/general-asr/asr-joiner.a7tv",
    "/ufs/npu/general-tts/vocoder.a7tv"};
  if(!path || !memchr(path,0,capacity))return false;
  for(unsigned i=0;i<sizeof(files)/sizeof(files[0]);++i)
    if(!strcmp(path,files[i]))return true;
  return false;
}
#endif
