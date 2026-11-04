/* SPDX-License-Identifier: Apache-2.0 */
/* CPU microbenchmark ONLY. Not NPU or board/realtime acceptance. */
#define _POSIX_C_SOURCE 200809L
#include <nuttx/crc32.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "../board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733/a733_tensor_crc.inc"
typedef uint32_t (*checksum_fn)(const uint8_t *,size_t,uint32_t);
static uint64_t now(void)
{
  struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));
  return (uint64_t)t.tv_sec*1000000000u+t.tv_nsec;
}
static uint32_t bench(checksum_fn fn,const uint8_t *data,size_t bytes,uint64_t *elapsed)
{
  uint32_t chain=0x134ba678;uint64_t start=now();
  for(unsigned n=0;n<64;++n)chain=fn(data,bytes,chain^n);
  *elapsed=now()-start;return chain;
}
int main(void)
{
  const size_t bytes=1024u*1024u;uint8_t *data=malloc(bytes);assert(data);
  uint32_t random=1;
  for(size_t i=0;i<bytes;++i){random=random*1664525u+1013904223u;data[i]=random>>24;}
  uint64_t legacy,slice;uint32_t a=bench(crc32part,data,bytes,&legacy);
  uint32_t b=bench(a733_tensor_crc32part,data,bytes,&slice);assert(a==b);
  printf("CPU_HOST_ONLY bytes=%zu rounds=64 chain=%08x legacy-ns=%llu slice8-ns=%llu ratio=%.3f; NOT board speed\n",
    bytes,a,(unsigned long long)legacy,(unsigned long long)slice,(double)legacy/slice);
  free(data);return 0;
}
