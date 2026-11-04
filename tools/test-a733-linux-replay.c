/* SPDX-License-Identifier: Apache-2.0 */
#define A733_TENSOR_LINUX_REPLAY 1
#define main replay_diagnostic_main
#include "a733-tensor-check.c"
#undef main
#include <assert.h>
#include <stdarg.h>

static int fault,allocs,frees,submits,waits,created,destroyed,closed;
static void *mapped[4];
static const uint32_t golden[4]={1,2,3,4};
int __real_open(const char *,int,...);
int __real_close(int);
int __wrap_open(const char *path,int flags,...)
{
  if(!strcmp(path,"/dev/vipcore"))return 99999;
  mode_t mode=0;
  if(flags&O_CREAT){va_list ap;va_start(ap,flags);mode=(mode_t)va_arg(ap,int);va_end(ap);}
  return __real_open(path,flags,mode);
}
int __wrap_close(int fd)
{
  if(fd==99999){++closed;return 0;}
  return __real_close(fd);
}
int __wrap_ioctl(int fd,unsigned long request,...)
{
  va_list ap;va_start(ap,request);struct envelope *e=va_arg(ap,struct envelope *);va_end(ap);
  assert(fd==99999 && request==30000ul);
  void *payload=(void *)(uintptr_t)e->data;
  if(e->command==0){if(fault==1)((uint32_t *)payload)[1]=1;}
  else if(e->command==6){
    if(fault==2 && allocs==1){e->error=1;return 0;}
    struct alloc *a=payload;assert(a->specified==1 && a->flags==0x718 && a->align==4096 && a->visibility==1);
    assert(allocs<3);mapped[allocs]=malloc(a->bytes);assert(mapped[allocs]);
    a->logical=(uintptr_t)mapped[allocs];a->id=(uint32_t)++allocs;
    if(fault==3)a->vip+=4096;
  }
  else if(e->command==7){unsigned id=*(uint32_t *)payload;assert(id>0 && id<=3 && mapped[id-1]);free(mapped[id-1]);mapped[id-1]=NULL;++frees;}
  else if(e->command==27){++created;((struct task *)payload)->id=1;}
  else if(e->command==29)++destroyed;
  else if(e->command==28){uint32_t *p=payload;assert(p[4]==3 && p[5]==8);}
  else if(e->command==5)++submits;
  else if(e->command==4){++waits;memcpy(mapped[1],golden,sizeof(golden));if(fault==4)((uint32_t *)mapped[1])[0]^=1;}
  else assert(e->command==1 || e->command==11);
  return 0;
}
int main(int argc,char **argv)
{
  assert(argc==2);struct a733_tensor_plan_s *p=calloc(1,sizeof(*p));assert(p);
  uint8_t image[96]={0};char path[1024],file[1100];
  p->header.regions=3;p->header.inputs=p->header.outputs=1;p->header.command_region=2;
  for(unsigned i=0;i<3;++i){p->region[i].vip=0x01010000+4096*i;p->region[i].bytes=i==2?64:16;p->region[i].flags=i==2?5:1;p->region[i].file_offset=i==2?32:16*i;}
  p->input[0].region=0;p->input[0].bytes=16;p->input[0].crc32=checksum(image,16,0);
  p->output[0].region=1;p->output[0].bytes=16;p->output[0].crc32=checksum((const uint8_t *)golden,16,0);
  for(int test=0;test<8;++test){
    fault=test<5?test:0;allocs=frees=submits=waits=created=destroyed=closed=0;
    snprintf(path,sizeof(path),"%s/case%d",argv[1],test);
    int result=replay(p,image,test==5,8,"-",path);
    if(test==0 || test==5){assert(result==0);}
    else if(test<5){assert(result<0);}
    assert(allocs==frees && created==destroyed && closed==1);
    if(test==1 || test==2 || test==3 || test==5)assert(submits==0 && waits==0);
    if(test==0){uint32_t actual[4];snprintf(file,sizeof(file),"%s/output0.bin",path);assert(read_exact(file,actual,16)==0 && !memcmp(actual,golden,16));}
    if(test==4){snprintf(file,sizeof(file),"%s/output0.bin",path);assert(access(file,F_OK)<0);}
  }
  /* Existing output dir and wrong native command size reject before fd open. */
  closed=0;assert(replay(p,image,0,16,"-",path)<0 && closed==0);
  assert(replay(p,image,0,8,"-",path)<0 && closed==0);
  free(p);puts("Linux prepared replay: 10 lifecycle/ownership/publication checks PASS (MOCK, no hardware)");
  return 0;
}
