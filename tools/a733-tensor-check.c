/* SPDX-License-Identifier: Apache-2.0 */
/* Host validator + explicitly requested Linux VIP2 prepared replay diagnostic.
 * Uses the same checked plan/byte-transport core as NuttX. Not a product API,
 * NBG parser, command relocator, raw MMIO driver, or hardware reset utility.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "a733_tensor_plan.h"
#ifdef A733_TENSOR_LINUX_REPLAY
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#endif

static uint32_t checksum(const uint8_t *data,size_t n,uint32_t crc)
{
  while(n--) { crc^=*data++; for(unsigned b=0;b<8;++b) crc=(crc>>1)^((crc&1)?0xedb88320u:0); }
  return crc;
}
static int read_exact(const char *path,void *data,size_t bytes)
{
  FILE *f=fopen(path,"rb");if(!f)return -errno;
  int ret=fread(data,1,bytes,f)==bytes && fgetc(f)==EOF && !ferror(f)?0:-EIO;
  if(fclose(f))ret=-EIO;
  return ret;
}
static int load(const char *path,struct a733_tensor_plan_s *p,uint8_t **image)
{
  FILE *f=fopen(path,"rb");long size;uint32_t pos;int ret;
  if(!f)return -errno;
  if(fseek(f,0,SEEK_END) || (size=ftell(f))<128 || size>A733_TENSOR_FILE_LIMIT) {fclose(f);return -EFBIG;}
  fclose(f);*image=malloc((size_t)size);if(!*image)return -ENOMEM;
  ret=read_exact(path,*image,(size_t)size);if(ret<0)return ret;
  memcpy(&p->header,*image,128);
  ret=a733_tensor_header_check(&p->header,(uint64_t)size,&pos);if(ret<0)return ret;
  pos=128;memcpy(p->region,*image+pos,p->header.regions*32);pos+=p->header.regions*32;
  memcpy(p->input,*image+pos,p->header.inputs*64);pos+=p->header.inputs*64;
  memcpy(p->output,*image+pos,p->header.outputs*64);
  ret=a733_tensor_plan_check(p,(uint64_t)size,checksum);if(ret<0)return ret;
  for(unsigned i=0;i<p->header.regions;++i) {
    const struct a733_tensor_region_s *r=&p->region[i];
    uint64_t end=(uint64_t)r->vip+((r->bytes+4095u)&~4095u);
    if(r->vip<=0x01001000u && end>0x01001000u)return -EADDRINUSE;
    if((r->flags&1) && checksum(*image+r->file_offset,r->bytes,0)!=r->crc32)return -EBADMSG;
  }
  for(unsigned i=0;i<p->header.inputs;++i) {
    const struct a733_tensor_desc_s *d=&p->input[i];
    if(checksum(*image+p->region[d->region].file_offset+d->offset,d->bytes,0)!=d->crc32)return -EBADMSG;
  }
  if(p->tail_vip==0x01001000u)return -EADDRINUSE;
  printf("validated inputs=%u outputs=%u regions=%u pool=%u command-vip=%08x entry=%u\n",
    p->header.inputs,p->header.outputs,p->header.regions,p->pool_bytes,
    p->region[p->header.command_region].vip,p->header.command_offset);
  return 0;
}

#ifdef A733_TENSOR_LINUX_REPLAY
/* Exact observed AArch64 Linux VIP2 ABI, checked against vendor vip_drv_share.h.
 * Never copy captured mem_id/CPU pointers/physical pages into requests. The
 * driver allocates new owner-scoped memory at explicitly requested VIP VAs.
 */
struct envelope {int32_t command,error;uint32_t bytes,reserved;uint64_t data;};
struct alloc {uint64_t logical;uint32_t id,pad0;uint64_t vip;uint32_t bytes,align,flags,pad1;uint64_t visibility;uint8_t specified,pad2[7];};
struct task {uint32_t id;char name[16];uint32_t count;};
_Static_assert(sizeof(struct envelope)==24,"envelope ABI");
_Static_assert(sizeof(struct alloc)==56,"allocation ABI");
_Static_assert(sizeof(struct task)==24,"task ABI");
static int call(int fd,int command,void *data,uint32_t bytes)
{
  struct envelope e={command,0,bytes,0,(uintptr_t)data};
  int rc=ioctl(fd,30000ul,&e);
  if(rc || e.error) {fprintf(stderr,"ioctl command=%d rc=%d errno=%d driver=%d\n",command,rc,errno,e.error);return -EIO;}
  return 0;
}
static int replay(struct a733_tensor_plan_s *p,uint8_t *image,int allocate_only,
                  uint32_t command_bytes,const char *inputs,const char *outputs)
{
  struct alloc *a=calloc(p->header.regions,sizeof(*a));
  struct a733_tensor_run_s *run=calloc(1,sizeof(*run));
  struct task task={0};void *regions[128]={0};void *incoming[64]={0};
  int fd=-1,ret=-ENOMEM,initialized=0,created=0;unsigned allocated=0;
  uint32_t init[2]={0,0x20003};char path[1024];
  if(!a || !run)goto done;
  if(!allocate_only && (!command_bytes || command_bytes%8 ||
     command_bytes+56u!=p->region[p->header.command_region].bytes || p->header.command_offset)) {ret=-EPROTO;goto done;}
  /* Complete file/input validation happens before opening the hardware fd. */
  for(unsigned i=0;!allocate_only && i<p->header.inputs;++i) {
    const struct a733_tensor_desc_s *d=&p->input[i];incoming[i]=malloc(d->bytes);
    if(!incoming[i]){ret=-ENOMEM;goto done;}
    if(!strcmp(inputs,"-"))memcpy(incoming[i],image+p->region[d->region].file_offset+d->offset,d->bytes);
    else {
      if(snprintf(path,sizeof(path),"%s/input%u.bin",inputs,i)>=(int)sizeof(path)){ret=-EFBIG;goto done;}
      ret=read_exact(path,incoming[i],d->bytes);if(ret<0)goto done;
    }
  }
  if(!allocate_only && mkdir(outputs,0700)) {ret=-errno;goto done;}
  fd=open("/dev/vipcore",O_RDWR|O_CLOEXEC);if(fd<0){ret=-errno;goto done;}
  ret=call(fd,0,init,sizeof(init));if(ret<0)goto done;initialized=1;
  if(init[1]!=0x20003){ret=-EPROTO;goto done;}
  for(unsigned i=0;i<p->header.regions;++i) {
    a[i].vip=p->region[i].vip;a[i].bytes=p->region[i].bytes;
    a[i].flags=0x710; /* uncached, map user+kernel, 32-bit VIP */
    /* The observed native map uses 1MiB/4KiB/64KiB/1MiB pages in windows
     * 0/1/2/3. Linux chooses a page type from PHYSICAL TABLE shape, not VIP
     * alignment. Random adjacent GFP pages can accidentally select64KiB for
     * a small buffer and conflict with the existing4KiB STLB. Force one
     * physically-contiguous extent for small/medium allocations, segmented
     * 1MiB extents for large ones. Reject any other captured profile. These
     * are owner-scoped allocations, not edits to shared SRAM/MMU state.
     */
    uint32_t window=p->region[i].vip>>24;
    if(window==1 && a[i].bytes<=32768u) {a[i].align=4096;a[i].flags|=8;}
    else if(window==2 && a[i].bytes>32768u && a[i].bytes<=524288u && !(a[i].vip&65535u))
      {a[i].align=65536;a[i].flags|=8;}
    else if((window==0 || window==3) && a[i].bytes>524288u && !(a[i].vip&0xfffffu))
      {a[i].align=0x100000;a[i].flags|=2;}
    else {ret=-EPROTO;goto done;}
    a[i].visibility=1;a[i].specified=1;
    ret=call(fd,6,&a[i],sizeof(a[i]));if(ret<0)goto done;
    ++allocated;
    if(!a[i].logical || !a[i].id || a[i].vip!=p->region[i].vip || a[i].bytes<p->region[i].bytes) {ret=-EPROTO;goto done;}
    regions[i]=(void *)(uintptr_t)a[i].logical;
    printf("allocated region=%u vip=%08x bytes=%u\n",i,p->region[i].vip,p->region[i].bytes);
  }
  if(allocate_only){ret=0;puts("allocation-only passed; no task created/submitted");goto done;}
  for(unsigned i=0;i<p->header.regions;++i) {
    memset(regions[i],0,p->region[i].bytes);
    if(p->region[i].flags&1)memcpy(regions[i],image+p->region[i].file_offset,p->region[i].bytes);
  }
  run->version=1;run->inputs=p->header.inputs;run->outputs=p->header.outputs;run->timeout_ms=5000;
  run->flags=!strcmp(inputs,"-")?1:0;
  for(unsigned i=0;i<run->inputs;++i)run->input[i].bytes=p->input[i].bytes;
  for(unsigned i=0;i<run->outputs;++i)run->output[i].bytes=p->output[i].bytes;
  ret=a733_tensor_input_check(p,run,(const void *const *)incoming,checksum);if(ret<0)goto done;
  a733_tensor_apply(p,regions,(const void *const *)incoming);a733_tensor_prefill(p,regions);
  for(unsigned i=0;i<p->header.regions;++i) {
    uint32_t cache[2]={a[i].id,2};ret=call(fd,11,cache,sizeof(cache));if(ret<0)goto done;
  }
  memcpy(task.name,"a7tv-linux",10);task.count=1;
  ret=call(fd,27,&task,sizeof(task));if(ret<0)goto done;created=1;
  uint32_t property[9]={task.id,0,0,0,a[p->header.command_region].id,command_bytes,0,0,0};
  ret=call(fd,28,property,sizeof(property));if(ret<0)goto done;
  uint32_t submit[9]={task.id,0,1,0,0,1,0,5000,1};uint32_t wait[2]={task.id,0};
  struct timespec start,end;clock_gettime(CLOCK_MONOTONIC,&start);
  ret=call(fd,5,submit,sizeof(submit));
  if(!ret)ret=call(fd,4,wait,sizeof(wait));
  clock_gettime(CLOCK_MONOTONIC,&end);
  if(ret<0) {
    /* No explicit free/reset/retry after uncertain hardware completion. fd
     * teardown is the Linux vendor driver's responsibility, not this client. */
    fprintf(stderr,"STOP: submit/wait failed; no retry/reset, preserve report and cold reboot before reuse\n");
    fflush(NULL);_exit(3);
  }
  printf("linux-replay hardware=vip2 status=0 elapsed-us=%lld\n",
    (long long)(end.tv_sec-start.tv_sec)*1000000+(end.tv_nsec-start.tv_nsec)/1000);
  for(unsigned i=0;i<p->header.regions;++i) {
    uint32_t cache[2]={a[i].id,3};ret=call(fd,11,cache,sizeof(cache));if(ret<0)goto done;
  }
  ret=a733_tensor_output_check(p,run,regions,checksum);if(ret<0)goto done;
  /* Publish only after every output passes, never overwrite prior outputs. */
  for(unsigned i=0;i<run->outputs;++i) {
    const struct a733_tensor_desc_s *d=&p->output[i];
    if(snprintf(path,sizeof(path),"%s/output%u.bin",outputs,i)>=(int)sizeof(path)){ret=-EFBIG;goto done;}
    int out=open(path,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);if(out<0){ret=-errno;goto done;}
    size_t n=0;
    while(n<d->bytes) {
      ssize_t k=write(out,(uint8_t *)regions[d->region]+d->offset+n,d->bytes-n);
      if(k<0 && errno==EINTR)continue;
      if(k<=0){ret=-EIO;break;}n+=(size_t)k;
    }
    if(close(out))ret=-EIO;
    if(ret<0)goto done;
    printf("output[%u] bytes=%u crc=%08x changed=%u\n",i,d->bytes,run->output_crc32[i],run->output_changed[i]);
  }
  puts("linux-replay-output-complete");ret=0;
done:
  if(created && call(fd,29,&task.id,4)<0)ret=-EIO;
  for(unsigned i=allocated;i>0;--i)if(call(fd,7,&a[i-1].id,4)<0)ret=-EIO;
  if(initialized && call(fd,1,NULL,0)<0)ret=-EIO;
  if(fd>=0)close(fd);
  for(unsigned i=0;i<64;++i)free(incoming[i]);
  free(run);free(a);return ret;
}
#endif
int main(int argc,char **argv)
{
  int ret;uint8_t *image=NULL;
  struct a733_tensor_plan_s *plan=calloc(1,sizeof(*plan));
  if(!plan)return 2;
  if(argc<2){fprintf(stderr,"Usage: tensor-check PACKAGE [--allocate | --run COMMAND_BYTES INPUTDIR|- NEW_OUTPUTDIR]\n");free(plan);return 2;}
  ret=load(argv[1],plan,&image);
#ifdef A733_TENSOR_LINUX_REPLAY
  if(!ret && argc>2) {
    if(argc==3 && !strcmp(argv[2],"--allocate"))ret=replay(plan,image,1,0,NULL,NULL);
    else if(argc==6 && !strcmp(argv[2],"--run")) {
      char *end=NULL;errno=0;unsigned long n=strtoul(argv[3],&end,10);
      if(errno || !end || *end || n>UINT32_MAX)ret=-EINVAL;
      else ret=replay(plan,image,0,(uint32_t)n,argv[4],argv[5]);
    } else ret=-EINVAL;
  }
#else
  if(argc!=2)ret=-EINVAL;
#endif
  if(ret<0)fprintf(stderr,"STOP status=%d\n",ret);
  free(image);free(plan);return ret<0?2:0;
}
