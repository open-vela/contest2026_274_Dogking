/* SPDX-License-Identifier: Apache-2.0 */
#ifndef AIPETLLM_Q8_WORKSPACE_H
#define AIPETLLM_Q8_WORKSPACE_H
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define AIPETLLM_Q8_CACHE_LIMIT (256u * 1024u)
/* Caller owns g_model_lock and waits for every pool worker before reuse/free.
 * No model pointers or quantized activations may escape a completed job. */
struct aipetllm_q8_workspace {
  void *data;
  size_t capacity;
  uint64_t allocations, reuses;
};
static void aipetllm_q8_wipe(void *data,size_t bytes) {
  volatile unsigned char *p=data;
  while(bytes--)*p++=0;
}
static int aipetllm_q8_reserve(struct aipetllm_q8_workspace *s,size_t bytes,void **out) {
  if(!s || !out || !bytes || bytes>AIPETLLM_Q8_CACHE_LIMIT)return -EINVAL;
  *out=NULL;
  if(s->capacity>=bytes){++s->reuses;*out=s->data;return 0;}
  void *p=malloc(bytes);if(!p)return -ENOMEM;
  if(s->data){aipetllm_q8_wipe(s->data,s->capacity);free(s->data);}
  s->data=p;s->capacity=bytes;++s->allocations;*out=p;return 0;
}
static void aipetllm_q8_release(struct aipetllm_q8_workspace *s) {
  if(s->data){aipetllm_q8_wipe(s->data,s->capacity);free(s->data);}
  memset(s,0,sizeof(*s));
}
#endif
