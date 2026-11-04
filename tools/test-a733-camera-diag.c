/* SPDX-License-Identifier: Apache-2.0 */
/* Host lifetime tests of the actual diagnostic fops, not a board run. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static int fail_alloc;
static unsigned mmio_reads, frees;
static void *test_calloc(size_t n,size_t size)
{return fail_alloc ? NULL : calloc(n,size);}
static void test_free(void *p){if(p)++frees;free(p);}
#define calloc test_calloc
#define free test_free
#include "../board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733/a733_camera_diag.c"
#undef calloc
#undef free

uint32_t getreg32(uintptr_t address)
{
  assert(address==0x02002518 || address==0x02002e8c ||
    address==0x02003804 || address==0x02003840 || address==0x02003844 ||
    address==0x02003860 || address==0x020025e4 || address==0x02000280 ||
    address==0x02000284 || address==0x02000290 || address==0x020002a0 ||
    address==0x020002b0);
  ++mmio_reads;return 0;
}

int register_driver(const char *path,const struct file_operations *ops,
                     mode_t mode,void *priv)
{
  assert(!strcmp(path,"/dev/a733-camera") && mode==0444 && !priv);
  assert(ops==&camera_ops && mmio_reads==0);
  return 0;
}

int main(void)
{
  assert(a733_camera_diag_initialize()==0 && mmio_reads==0);
  struct file first={0};
  fail_alloc=1;
  assert(camera_open(&first)==-ENOMEM && !first.f_priv && mmio_reads==0);
  fail_alloc=0;
  assert(camera_open(&first)==0 && mmio_reads==12);
  char text[2048], small[3];
  assert(camera_read(&first,small,3)==3 && !memcmp(small,"A73",3));
  struct file copy=first;
  assert(camera_open(&copy)==0 && copy.f_priv!=first.f_priv && mmio_reads==12);
  fail_alloc=1;
  struct file failed=first;
  assert(camera_open(&failed)==-ENOMEM && !failed.f_priv && frees==0);
  fail_alloc=0;
  assert(camera_close(&first)==0 && !first.f_priv && frees==1);
  assert(camera_read(&copy,NULL,0)==0);
  assert(camera_read(&copy,NULL,1)==-EINVAL);
  copy.f_pos=-1;assert(camera_read(&copy,text,1)==-EINVAL);copy.f_pos=3;
  ssize_t n=camera_read(&copy,text,sizeof(text)-1);
  assert(n>0);text[n]=0;
  assert(strstr(text,"NOT video capture") && strstr(text,"NOT power/clock qualification"));
  assert(camera_read(&copy,text,1)==0);
  assert(camera_close(&copy)==0 && frees==2);
  puts("PASS: actual diagnostic fops passive registration, partial reads, per-open/dup lifetime, OOM, close; HOST mocks only");
  return 0;
}
