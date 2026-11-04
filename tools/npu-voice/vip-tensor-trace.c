/* SPDX-License-Identifier: Apache-2.0 */
/* Private Linux diagnostic bridge for the OBSERVED A733 AArch64 VIPLite
 * ioctl ABI. Not an NBG loader/compiler or general preload utility.
 * Observes one single-owner reference process; never rewrites ioctl payloads,
 * commands, addresses, network timeouts, or driver return codes.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>

#define MAX_ALLOCS 256u
#ifndef VIP_TENSOR_TRACE_MAX_BUFFER
#define VIP_TENSOR_TRACE_MAX_BUFFER (32u*1024u*1024u)
#endif
/* General-ASR's verified 41MB network needs a larger *diagnostic file*
 * bound. This does not change allocations, ioctl payloads or RTOS memory. */
#if VIP_TENSOR_TRACE_MAX_BUFFER > (64u*1024u*1024u)
#error "trace allocation bound must not exceed 64 MiB"
#endif
#define MAX_BUFFER VIP_TENSOR_TRACE_MAX_BUFFER
#define MAX_SNAPSHOT (128u*1024u*1024u)
#define MAX_TOTAL (512u*1024u*1024u)
struct request { int32_t command,error; uint32_t bytes,reserved; uint64_t buffer; };
struct allocation { uint32_t id,active; uint64_t logical,vip,bytes; };
_Static_assert(sizeof(struct request)==24,"A733 observed AArch64 ioctl ABI");
static struct allocation allocations[MAX_ALLOCS];
static int logfd=-1,initialized,failed;
static uint64_t sequence,snapshots,dumped;
static char directory[512];
static uint64_t u64(const unsigned char *p) { uint64_t v; memcpy(&v,p,8); return v; }
static uint32_t u32(const unsigned char *p) { uint32_t v; memcpy(&v,p,4); return v; }
static int copy(void *out,uint64_t address,size_t bytes)
{
  struct iovec dst={out,bytes},src={(void *)(uintptr_t)address,bytes};
  ssize_t count=process_vm_readv(getpid(),&dst,1,&src,1,0);
  if(count==(ssize_t)bytes) return 0;
  if(count>=0) errno=EIO;
  return -1;
}
static void init(void)
{
  const char *requested;
  char path[600];
  if(initialized) return;
  initialized=1;
  requested=getenv("VIP_TENSOR_TRACE_DIR");
  if(!requested || !*requested || strlen(requested)>=sizeof(directory)) { failed=1; return; }
  strcpy(directory,requested);
  struct stat st;
  if(lstat(directory,&st)<0 || !S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)) { failed=1; return; }
  snprintf(path,sizeof(path),"%s/trace.jsonl",directory);
  logfd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
  if(logfd<0) { failed=1; return; }
  if(dprintf(logfd,"{\"kind\":\"header\",\"schema\":1,\"pid\":%ld,\"max_total\":%u}\n",(long)getpid(),MAX_TOTAL)<0) failed=1;
}
static int dump(const char *name,uint64_t address,uint64_t bytes)
{
  unsigned char data[65536];
  char path[800];
  uint64_t offset=0;
  unsigned fallback_chunks=0;
  int fd,ret=0,first_read_errno=0,io_errno=0;
  if(logfd<0 || !bytes || bytes>MAX_BUFFER || bytes>MAX_TOTAL-dumped || address>UINT64_MAX-bytes)
    { failed=1; return -1; }
  if(snprintf(path,sizeof(path),"%s/%s",directory,name)>=(int)sizeof(path)) { failed=1; return -1; }
  fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
  if(fd<0) { failed=1; return -1; }
  while(offset<bytes) {
    size_t n=bytes-offset>sizeof(data) ? sizeof(data) : (size_t)(bytes-offset);
    const void *source=data;
    if(copy(data,address+offset,n)) {
      /* VM_IO/PFNMAP VIP CPU mappings reject process_vm_readv (FOLL_GET).
       * write's copy_from_user can read these valid mappings without pinning
       * pages. This is the same bounded path used by the earlier VIP tracer.
       * Never memcpy an untrusted pointer or install process signal handlers.
       * A bad/revoked mapping returns EFAULT and must fail the whole trace.
       */
      if(!fallback_chunks) first_read_errno=errno;
      ++fallback_chunks;
      source=(const void *)(uintptr_t)(address+offset);
    }
    size_t written=0;
    while(written<n) {
      ssize_t count=write(fd,(const unsigned char *)source+written,n-written);
      if(count<0 && errno==EINTR) continue;
      if(count<=0) { io_errno=count<0?errno:EIO; ret=-1; break; }
      written+=(size_t)count;
      dumped+=(uint64_t)count;
    }
    if(ret) break;
    offset+=n;
  }
  if(close(fd)<0) { io_errno=errno; ret=-1; }
  if(fallback_chunks && dprintf(logfd,
     "{\"kind\":\"read-fallback\",\"file\":\"%s\",\"method\":\"write-copy-from-user\",\"chunks\":%u,\"read_errno\":%d,\"status\":%d}\n",
     name,fallback_chunks,first_read_errno,ret)<0) ret=-1;
  if(ret) dprintf(logfd,
     "{\"kind\":\"read-error\",\"file\":\"%s\",\"logical\":\"%016" PRIx64 "\",\"bytes\":%" PRIu64 ",\"completed_chunks_bytes\":%" PRIu64 ",\"errno\":%d}\n",
     name,address,bytes,offset,io_errno);
  if(ret) failed=1;
  return ret;
}
static void payload(uint64_t seq,const char *phase,const struct request *r)
{
  char name[96];
  if(!r->bytes) return;
  if(r->bytes>4096 || !r->buffer) { failed=1; return; }
  snprintf(name,sizeof(name),"ioctl-%06" PRIu64 "-%s-cmd%d.bin",seq,phase,r->command);
  if(!dump(name,r->buffer,r->bytes) &&
     dprintf(logfd,"{\"kind\":\"payload\",\"seq\":%" PRIu64 ",\"phase\":\"%s\",\"command\":%d,\"error\":%d,\"file\":\"%s\",\"bytes\":%u}\n",
             seq,phase,r->command,r->error,name,r->bytes)<0) failed=1;
}
static void track(const struct request *r,uint64_t seq)
{
  unsigned char data[256];
  uint32_t id;
  struct allocation *a=NULL;
  size_t n=r->bytes<sizeof(data) ? r->bytes : sizeof(data);
  if(r->command!=6 && r->command!=7 && r->command!=23) return;
  if(!n || !r->buffer || copy(data,r->buffer,n)) { failed=1; return; }
  if(r->command==6 && n>=49) id=u32(data+8);
  else if(r->command==23 && n>=12) id=u32(data+8);
  else if(r->command==7 && n>=4) id=u32(data);
  else { failed=1; return; }
  for(unsigned i=0;i<MAX_ALLOCS;++i)
    if(allocations[i].active && allocations[i].id==id) { a=&allocations[i]; break; }
  if(r->command==7) {
    if(a) a->active=0;
    return;
  }
  if(!a) for(unsigned i=0;i<MAX_ALLOCS;++i) if(!allocations[i].active) {
    a=&allocations[i]; memset(a,0,sizeof(*a)); a->id=id; a->active=1; break;
  }
  if(!a) { failed=1; return; }
  a->logical=u64(data);
  if(r->command==6) { a->vip=u64(data+16); a->bytes=u32(data+24); }
  if(dprintf(logfd,"{\"kind\":\"allocation\",\"seq\":%" PRIu64 ",\"id\":%u,\"logical\":\"%016" PRIx64 "\",\"vip\":\"%016" PRIx64 "\",\"bytes\":%" PRIu64 "}\n",
             seq,id,a->logical,a->vip,a->bytes)<0) failed=1;
}
static void snapshot(uint64_t seq,const char *phase)
{
  uint64_t bytes=0;
  ++snapshots;
  for(unsigned i=0;i<MAX_ALLOCS;++i) {
    const struct allocation *a=&allocations[i];
    char name[128];
    if(!a->active || !a->logical || !a->bytes) continue;
    if(a->bytes>MAX_SNAPSHOT-bytes) { failed=1; return; }
    snprintf(name,sizeof(name),"snapshot-%06" PRIu64 "-%s-mem%u-vip%016" PRIx64 ".bin",snapshots,phase,a->id,a->vip);
    if(dump(name,a->logical,a->bytes)) return;
    bytes+=a->bytes;
    if(dprintf(logfd,"{\"kind\":\"snapshot\",\"seq\":%" PRIu64 ",\"snapshot\":%" PRIu64 ",\"phase\":\"%s\",\"id\":%u,\"logical\":\"%016" PRIx64 "\",\"vip\":\"%016" PRIx64 "\",\"bytes\":%" PRIu64 ",\"file\":\"%s\"}\n",
                seq,snapshots,phase,a->id,a->logical,a->vip,a->bytes,name)<0) failed=1;
  }
}
int ioctl(int fd,unsigned long request,...)
{
  va_list args;
  void *argument;
  struct request before,after;
  int observed=0,previous=errno;
  uint64_t seq=0;
  long status;
  va_start(args,request); argument=va_arg(args,void *); va_end(args);
  if(request==30000ul && argument) {
    init();
    if(logfd>=0 && !copy(&before,(uint64_t)(uintptr_t)argument,sizeof(before))) {
      observed=1; seq=++sequence; payload(seq,"pre",&before);
      if(before.command==5) snapshot(seq,"pre-submit");
    } else failed=1;
  }
  errno=previous;
  status=syscall(SYS_ioctl,fd,request,argument);
  previous=errno;
  if(observed && !copy(&after,(uint64_t)(uintptr_t)argument,sizeof(after))) {
    payload(seq,"post",&after);
    if(status==0 && after.error==0) {
      track(&after,seq);
      if(after.command==4) snapshot(seq,"post-wait");
    }
    if(dprintf(logfd,"{\"kind\":\"return\",\"seq\":%" PRIu64 ",\"command\":%d,\"rc\":%ld,\"errno\":%d,\"error\":%d}\n",
                seq,after.command,status,status<0?previous:0,after.error)<0) failed=1;
  } else if(observed) failed=1;
  errno=previous;
  return (int)status;
}
__attribute__((destructor)) static void finish(void)
{
  if(logfd>=0) {
    dprintf(logfd,"{\"kind\":\"final\",\"failed\":%d,\"snapshots\":%" PRIu64 ",\"dumped_bytes\":%" PRIu64 "}\n",failed,snapshots,dumped);
    close(logfd); logfd=-1;
  }
}
