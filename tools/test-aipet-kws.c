/* SPDX-License-Identifier: Apache-2.0 */
#include "voice_kws.h"
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct aipet_kws_config config = {75, 15, 12, 12, .0001f, .12f, 2};
static struct aipet_kws_word words[] = {
  {4,{3,4,5,6},2.0f,.18f}, {6,{3,4,5,6,7,8},2.0f,.18f}};
static float logits[AIPET_KWS_CONTEXTS*AIPET_KWS_VOCAB];
static int32_t pairs[AIPET_KWS_CONTEXTS*2];
static void *state;
static unsigned checks;
static struct aipet_kws_hit step(int token, float value)
{
  struct aipet_kws_hit hit;
  int rows = aipet_kws_contexts(state, pairs, AIPET_KWS_CONTEXTS*2);
  int i, j;
  assert(rows > 0 && rows <= AIPET_KWS_CONTEXTS);
  for (i=0; i<rows; ++i) {
    for (j=0; j<AIPET_KWS_VOCAB; ++j) logits[i*AIPET_KWS_VOCAB+j]=-20;
    logits[i*AIPET_KWS_VOCAB]=5;
    if (token) logits[i*AIPET_KWS_VOCAB+token]=value;
  }
  assert(aipet_kws_advance(state, logits, (size_t)rows, &hit)==0);
  return hit;
}
static void init(size_t count)
{
  assert(aipet_kws_init(state,aipet_kws_bytes(),words,count,&config)==0);
}
int main(void)
{
  struct aipet_kws_hit hit;
  void *snapshot;
  int rows, i;
  state=calloc(1,aipet_kws_bytes()); snapshot=malloc(aipet_kws_bytes());
  assert(state && snapshot);
  init(1);
  for (i=0; i<1000; ++i) assert(step(0,0).word==-1);
  ++checks;
  /* Old hard .08 start gate would reject this .06 first phoneme. */
  init(1);
  assert(step(3,2.25f).word==-1);
  assert(step(4,8).word==-1); assert(step(5,8).word==-1);
  hit=step(6,8); assert(hit.word==0 && hit.mean_probability>.18f);
  assert(step(3,8).word==-1); /* cooldown */
  ++checks;
  /* Actual word threshold is enforced, even when accumulated bias is high. */
  init(1);
  for (i=3; i<=6; ++i) assert(step(i,2.5f).word==-1);
  for (i=0; i<20; ++i) assert(step(0,0).word==-1);
  ++checks;
  /* Preserve blank as an alternative when another phoneme is premature. */
  init(1);
  assert(step(3,8).word==-1); assert(step(4,2).word==-1);
  assert(step(4,8).word==-1); assert(step(5,8).word==-1);
  assert(step(6,8).word==0);
  ++checks;
  /* Partial words expire; an unrelated later suffix must not complete one. */
  init(1); step(3,8); step(4,8);
  for(i=0;i<30;++i) assert(step(0,0).word==-1);
  assert(step(5,8).word==-1); assert(step(6,8).word==-1);
  ++checks;
  /* Long word wins over a short terminal prefix; standalone prefix waits. */
  init(2);
  for(i=3;i<=7;++i) assert(step(i,8).word==-1);
  assert(step(8,8).word==1);
  init(2);
  for(i=3;i<=6;++i) assert(step(i,8).word==-1);
  for(i=0;i<11;++i) assert(step(0,0).word==-1);
  assert(step(0,0).word==0);
  ++checks;
  init(1); step(3,8);
  memcpy(snapshot,state,aipet_kws_bytes());
  assert(aipet_kws_contexts(state,pairs,1)==-ENOSPC);
  rows=aipet_kws_contexts(state,pairs,AIPET_KWS_CONTEXTS*2);
  logits[0]=NAN;
  assert(aipet_kws_advance(state,logits,(size_t)rows,&hit)==-ERANGE);
  assert(!memcmp(snapshot,state,aipet_kws_bytes()));
  assert(aipet_kws_advance(state,logits,(size_t)rows+1,&hit)==-EINVAL);
  assert(aipet_kws_reset(state)==0);
  assert(aipet_kws_contexts(state,pairs,AIPET_KWS_CONTEXTS*2)==1);
  ++checks;
  config.max_span=0;
  assert(aipet_kws_init(state,aipet_kws_bytes(),words,1,&config)==-EINVAL);
  config.max_span=75; words[0].tokens[0]=2;
  assert(aipet_kws_init(state,aipet_kws_bytes(),words,1,&config)==-EINVAL);
  assert(aipet_kws_init((char *)state+1,aipet_kws_bytes(),words,1,&config)==-EINVAL);
  ++checks;
  free(snapshot); free(state);
  printf("KWS CPU search: %u groups passed; synthetic tests, no NPU/accuracy claim\n",checks);
  return 0;
}
