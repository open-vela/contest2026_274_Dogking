/* SPDX-License-Identifier: Apache-2.0 */
/* Local host oracle bridge, never a board driver. Little-endian binary stdin
 * and stdout, no shell commands, filesystem writes, network, or NPU access.
 * D=context i64[2], J=encoder+decoder f32[1024]; reply i32 status + FP32.
 * K=chunk accepted, R=u32 count + (token,frame)*count, E=i32 error.
 */
#include "voice_asr_decode.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
static int exact(void *v,size_t size) { return fread(v,1,size,stdin)==size?0:-EIO; }
static int send(const void *v,size_t size) { return fwrite(v,1,size,stdout)==size?0:-EIO; }
static int decoder(void *cookie,const int64_t context[2],float *out)
{
  int32_t rc;(void)cookie;
  if(send("D",1) || send(context,16) || fflush(stdout) || exact(&rc,4))return -EIO;
  return rc?rc:exact(out,512*4);
}
static int joiner(void *cookie,const float *encoder,const float *dec,float *out)
{
  int32_t rc;(void)cookie;
  if(send("J",1) || send(encoder,512*4) || send(dec,512*4) || fflush(stdout) || exact(&rc,4))return -EIO;
  return rc?rc:exact(out,6254*4);
}
static int cancel(void *p) { (void)p;return 0; }
int main(void)
{
  const struct aipet_asr_ops ops={decoder,joiner,cancel};
  struct aipet_asr_decoder *s=NULL;int32_t rc;uint32_t frames;
  float *rows=malloc(24*512*4);unsigned short endian=1;
  if(!rows || *(unsigned char *)&endian!=1 || sizeof(float)!=4)return 2;
  rc=aipet_asr_decoder_open(&ops,NULL,&s);
  while(!rc) {
    rc=exact(&frames,4);if(rc || !frames)break;
    if(frames>24) {rc=-E2BIG;break;}
    rc=exact(rows,frames*512*4);
    if(!rc)rc=aipet_asr_decoder_accept(s,rows,frames);
    if(!rc && (send("K",1) || fflush(stdout)))rc=-EIO;
  }
  if(!rc)rc=aipet_asr_decoder_finish(s);
  if(!rc) {
    uint32_t tokens[1024],positions[1024],n;size_t used=0;
    rc=aipet_asr_decoder_result(s,tokens,positions,1024,&used);n=(uint32_t)used;
    if(!rc) {
      send("R",1);send(&n,4);
      for(size_t i=0;i<used;++i){send(tokens+i,4);send(positions+i,4);}
    }
  }
  if(rc) {send("E",1);send(&rc,4);}
  fflush(stdout);aipet_asr_decoder_close(s);free(rows);return rc?2:0;
}
