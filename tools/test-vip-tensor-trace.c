/* SPDX-License-Identifier: Apache-2.0 */
#define ioctl observed_ioctl_for_test
#include "npu-voice/vip-tensor-trace.c"
#undef ioctl
#include <assert.h>
#include <sys/mman.h>

/* Force VM_IO-like EFAULT for ordinary RAM to exercise the real kernel
 * write/copy_from_user fallback under ASan/UBSan, not a mocked writer. */
static int reject_readv;
ssize_t __real_process_vm_readv(pid_t,const struct iovec *,unsigned long,
                               const struct iovec *,unsigned long,unsigned long);
ssize_t __wrap_process_vm_readv(pid_t pid,const struct iovec *local,unsigned long lc,
                               const struct iovec *remote,unsigned long rc,unsigned long flags)
{
  if(reject_readv) { errno=EFAULT; return -1; }
  return __real_process_vm_readv(pid,local,lc,remote,rc,flags);
}

int main(int argc,char **argv)
{
  unsigned char memory[64]={0},payload_data[56]={0},actual[64];
  uint64_t address=(uint64_t)(uintptr_t)memory,vip=0x04000000;
  uint32_t id=7,size=64;
  struct request r={6,0,56,0,(uint64_t)(uintptr_t)payload_data};
  char path[1024]; int fd;
  assert(argc==2 && !setenv("VIP_TENSOR_TRACE_DIR",argv[1],1));
  init(); assert(logfd>=0 && !failed);
  memcpy(payload_data,&address,8); memcpy(payload_data+8,&id,4);
  memcpy(payload_data+16,&vip,8); memcpy(payload_data+24,&size,4);
  track(&r,1); snapshot(2,"pre-submit"); assert(!failed && snapshots==1);
  memory[3]=0x42; snapshot(3,"post-wait"); assert(!failed && snapshots==2);
  snprintf(path,sizeof(path),"%s/snapshot-000001-pre-submit-mem7-vip0000000004000000.bin",argv[1]);
  fd=open(path,O_RDONLY); assert(fd>=0 && read(fd,actual,64)==64 && actual[3]==0); close(fd);
  snprintf(path,sizeof(path),"%s/snapshot-000002-post-wait-mem7-vip0000000004000000.bin",argv[1]);
  fd=open(path,O_RDONLY); assert(fd>=0 && read(fd,actual,64)==64 && actual[3]==0x42); close(fd);
  assert(copy(actual,1,64)<0); /* unreadable address is rejected, never dereferenced */
  reject_readv=1;
  failed=0;
  assert(dump("pfnmap-fallback.bin",address,64)==0 && !failed);
  snprintf(path,sizeof(path),"%s/pfnmap-fallback.bin",argv[1]);
  fd=open(path,O_RDONLY); assert(fd>=0 && read(fd,actual,64)==64 && !memcmp(actual,memory,64)); close(fd);
  unsigned char *pages=mmap(NULL,131072,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
  assert(pages!=MAP_FAILED);
  memset(pages,0x6d,131072);
  assert(dump("pfnmap-multi.bin",(uintptr_t)pages,131072)==0 && !failed);
  snprintf(path,sizeof(path),"%s/pfnmap-multi.bin",argv[1]);
  fd=open(path,O_RDONLY); assert(fd>=0);
  for(size_t offset=0;offset<131072;offset+=64)
    assert(read(fd,actual,64)==64 && !memcmp(actual,pages+offset,64));
  assert(lseek(fd,-64,SEEK_END)==131008 &&
                                read(fd,actual,64)==64 && actual[63]==0x6d); close(fd);
  assert(!mprotect(pages,131072,PROT_NONE));
  assert(dump("pfnmap-unreadable.bin",(uintptr_t)pages,64)<0 && failed);
  assert(!munmap(pages,131072));
  assert(dump("invalid-fallback.bin",1,64)<0 && failed);
  reject_readv=0;
  assert(dump("exclusive.bin",address,64)==0);
  assert(dump("exclusive.bin",address,64)<0 && failed); /* no overwrite */
  assert(dump("too-large.bin",address,MAX_BUFFER+1ul)<0);
  finish(); assert(logfd==-1);
  puts("Bounded Linux tracer tests PASS (including forced EFAULT/copy_from_user fallback, no ioctl/NPU calls)");
  return 0;
}
