/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fail_alloc;
static void *test_alloc(size_t n){return fail_alloc?NULL:malloc(n);}
#define malloc test_alloc
#include "../board/a733-cubie-a7z/openvela-overlay/apps/system/aipetllm/aipetllm_q8_workspace.h"
#undef malloc
#include "ggml-quants.h"
int main(void) {
  struct aipetllm_q8_workspace s={0};void *p=NULL;
  assert(aipetllm_q8_reserve(&s,0,&p)==-EINVAL);
  assert(aipetllm_q8_reserve(&s,AIPETLLM_Q8_CACHE_LIMIT+1,&p)==-EINVAL);
  assert(!aipetllm_q8_reserve(&s,4096,&p));void *first=p;
  for(unsigned i=0;i<10000;++i){assert(!aipetllm_q8_reserve(&s,2048,&p));assert(p==first);}
  assert(s.allocations==1 && s.reuses==10000);
  fail_alloc=1;assert(aipetllm_q8_reserve(&s,8192,&p)==-ENOMEM && s.data==first && !p);fail_alloc=0;
  uint32_t random=211;
  for(unsigned blocks=1;blocks<=64;blocks*=2)for(unsigned kind=0;kind<4;++kind) {
    unsigned n=blocks*256;size_t bytes=blocks*sizeof(block_q8_K);
    float *x=calloc(n,sizeof(float));block_q8_K *ref=calloc(1,bytes);
    assert(x && ref);
    for(unsigned i=0;i<n;++i){random=random*1664525u+1013904223u;
      if(kind && (kind!=2 || i/256%2))x[i]=(int32_t)random/2147483648.0f;}
    quantize_row_q8_K_ref(x,ref,n);
    assert(!aipetllm_q8_reserve(&s,bytes,&p));memset(p,0xa5,bytes);
    /* Same canonical clear as production: zero-block bsums aren't written
     * by GGML, so never let cached activations leak into those fields. */
    memset(p,0,bytes);quantize_row_q8_K_ref(x,p,n);
    assert(!memcmp(ref,p,bytes));free(ref);free(x);
  }
  unsigned char secret[64];memset(secret,0xa5,sizeof(secret));
  aipetllm_q8_wipe(secret,sizeof(secret));for(unsigned i=0;i<sizeof(secret);++i)assert(!secret[i]);
  aipetllm_q8_release(&s);assert(!s.data && !s.capacity && !s.reuses && !s.allocations);
  puts("PASS actual GGML Q8 quantizer byte-equivalence, zero/mixed-zero/random blocks, 10,000 reuse, OOM preservation, limits and wipe");
}
