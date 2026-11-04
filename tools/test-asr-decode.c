/* SPDX-License-Identifier: Apache-2.0 */
#include "voice_asr_decode.h"
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
struct mock { unsigned dec, join; int stop, bad, fail; int64_t previous[2]; };
static int cancel(void *p) { return ((struct mock *)p)->stop; }
static int decoder(void *p,const int64_t c[2],float *v)
{
  struct mock *m=p;unsigned i;
  ++m->dec;m->previous[0]=c[0];m->previous[1]=c[1];
  for(i=0;i<512;++i)v[i]=(float)c[i%2];
  return m->fail;
}
static int joiner(void *p,const float *e,const float *d,float *logits)
{
  struct mock *m=p;unsigned i;(void)d;++m->join;
  for(i=0;i<6254;++i)logits[i]=-10;
  logits[(unsigned)e[0]]=1;
  if(m->bad)logits[37]=NAN;
  return 0;
}
int main(void)
{
  const struct aipet_asr_ops ops={decoder,joiner,cancel};
  struct aipet_asr_decoder *s=NULL;struct mock m={0};
  float *e=calloc(1025*512,sizeof(float));uint32_t t[1024],p[1024];size_t used=8;
  assert(e && aipet_asr_decoder_open(NULL,NULL,&s)==-ENODEV && !s);
  assert(aipet_asr_decoder_open(&ops,&m,&s)==0 && m.dec==1);
  assert(aipet_asr_decoder_result(s,t,p,1024,&used)==-EAGAIN && used==0);
  e[0]=4;e[512]=0;e[1024]=2;e[1536]=9;
  assert(!aipet_asr_decoder_accept(s,e,2));
  assert(!aipet_asr_decoder_accept(s,e+1024,2));
  assert(m.dec==3 && m.join==4 && m.previous[0]==4 && m.previous[1]==9);
  assert(!aipet_asr_decoder_finish(s));
  assert(aipet_asr_decoder_result(s,t,p,1,&used)==-ENOSPC && !used);
  assert(!aipet_asr_decoder_result(s,t,p,1024,&used) && used==2);
  assert(t[0]==4 && t[1]==9 && p[0]==0 && p[1]==3);
  assert(aipet_asr_decoder_accept(s,e,1)==-EPIPE);
  aipet_asr_decoder_close(s);
  m=(struct mock){0};assert(!aipet_asr_decoder_open(&ops,&m,&s));
  m.bad=1;assert(aipet_asr_decoder_accept(s,e,1)==-EILSEQ);
  m.bad=0;assert(aipet_asr_decoder_finish(s)==-EILSEQ);
  assert(aipet_asr_decoder_result(s,t,p,1024,&used)==-EILSEQ && !used);
  aipet_asr_decoder_close(s);
  m=(struct mock){0};assert(!aipet_asr_decoder_open(&ops,&m,&s));
  m.stop=-ECANCELED;assert(aipet_asr_decoder_accept(s,e,1)==-ECANCELED);
  assert(m.join==0);aipet_asr_decoder_close(s);
  m=(struct mock){0};assert(!aipet_asr_decoder_open(&ops,&m,&s));
  assert(aipet_asr_decoder_accept(s,e,1025)==-E2BIG);aipet_asr_decoder_close(s);
  m=(struct mock){0};assert(!aipet_asr_decoder_open(&ops,&m,&s));
  m.fail=-ETIMEDOUT;assert(aipet_asr_decoder_accept(s,e,1)==-ETIMEDOUT);
  assert(aipet_asr_decoder_result(s,t,p,1024,&used)==-ETIMEDOUT && !used);
  aipet_asr_decoder_close(s);
  m=(struct mock){0};assert(!aipet_asr_decoder_open(&ops,&m,&s));
  e[0]=1;assert(aipet_asr_decoder_accept(s,e,1)==-EILSEQ);aipet_asr_decoder_close(s);
  m=(struct mock){0};assert(!aipet_asr_decoder_open(&ops,&m,&s));
  e[1]=INFINITY;assert(aipet_asr_decoder_accept(s,e,1)==-EILSEQ && m.join==0);
  aipet_asr_decoder_close(s);free(e);puts("ASR decoder bounds/context/error/publication tests PASS");
  return 0;
}
