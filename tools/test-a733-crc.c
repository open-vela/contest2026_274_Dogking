/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include <nuttx/crc32.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include "../board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733/a733_tensor_crc.inc"
static uint32_t independent(const uint8_t *p,size_t n,uint32_t seed)
{
  while(n--){seed^=*p++;for(unsigned bit=0;bit<8;++bit)seed=(seed>>1)^((seed&1u)?0xedb88320u:0);}
  return seed;
}
int main(void)
{
  uint8_t *data=malloc(1024*1024+16);assert(data);uint32_t seed=0x1367abcd;
  for(unsigned i=0;i<1024*1024+16;++i){seed=seed*1664525u+1013904223u;data[i]=seed>>24;}
  unsigned count=0;
  for(unsigned offset=0;offset<8;++offset)for(unsigned n=0;n<=257;++n){
    uint32_t initial=seed^(n*1664525u)^offset;
    uint32_t expected=independent(data+offset,n,initial);
    assert(crc32part(data+offset,n,initial)==expected);
    assert(crc32part(data+offset+n/2,n-n/2,crc32part(data+offset,n/2,initial))==expected);
    assert(a733_tensor_crc32part(data+offset,n,initial)==expected);
    assert(a733_tensor_crc32part(data+offset+n/2,n-n/2,a733_tensor_crc32part(data+offset,n/2,initial))==expected);++count;
  }
  for(unsigned n=4096;n<=1024*1024;n*=2){assert(crc32part(data,n,0)==independent(data,n,0));assert(a733_tensor_crc32part(data,n,0)==independent(data,n,0));++count;}
  assert(crc32part((const uint8_t *)"123456789",9,0)==0x2dfd2d88u);
  assert(a733_tensor_crc32part((const uint8_t *)"123456789",9,0)==0x2dfd2d88u);
  assert(a733_tensor_crc32part(NULL,0,seed)==seed);
  for(unsigned s=0;s<8;++s)for(unsigned b=0;b<256;++b) {
    uint8_t byte=s?0:b;
    assert(g_a733_crc_slice[s][b]==independent(&byte,1,s?g_a733_crc_slice[s-1][b]:0));
  }
  size_t page=(size_t)sysconf(_SC_PAGESIZE);assert(page>=1024);
  uint8_t *guard=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(guard!=MAP_FAILED);
  assert(!mprotect(guard+page,page,PROT_NONE));
  for(size_t i=0;i<page;++i)guard[i]=(uint8_t)i;
  for(unsigned n=0;n<=1024;++n)assert(a733_tensor_crc32part(guard+page-n,n,seed)==independent(guard+page-n,n,seed));
  assert(!munmap(guard,page*2));
  free(data);printf("Official + slice8 CRC vs independent bitwise: %u stream groups, 2048 table entries, 1025 guard-page tails PASS\n",count);
}
