/* SPDX-License-Identifier: Apache-2.0 */
#include <system/a733_tensor_client.h>
#include <nuttx/npu/a733_vip2.h>
#include <nuttx/crc32.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "a733_tensor_crc.inc"

struct a733_tensor_client {
  int fd;
  bool poisoned, busy, resident, full_clean, crc_legacy;
  struct a733_tensor_client_contract contract;
  struct a733_npu_alloc_s input, output;
  struct a733_tensor_run_s run;
};
static int failure(void) { return errno ? -errno : -EIO; }
static uint32_t aligned(uint32_t bytes) { return (bytes+63u)&~63u; }
static int total(const uint32_t *sizes, uint32_t count, uint32_t *bytes)
{
  uint32_t sum=0;
  if(!count || count>A733_TENSOR_CLIENT_IO) return -EINVAL;
  for(uint32_t i=0;i<count;++i) {
    if(!sizes[i] || sizes[i]>A733_TENSOR_IO_LIMIT ||
       aligned(sizes[i])>A733_TENSOR_IO_LIMIT-sum) return -E2BIG;
    sum+=aligned(sizes[i]);
  }
  *bytes=sum; return 0;
}
static void *address(const struct a733_npu_alloc_s *buffer)
{
  return (void *)(uintptr_t)buffer->cpu_address;
}
int a733_tensor_client_close(struct a733_tensor_client *s)
{
  int ret;
  if(!s) return -EINVAL;
  if(s->busy) return -EBUSY;
  if(s->input.cpu_address) memset(address(&s->input),0,s->input.size);
  if(s->output.cpu_address) memset(address(&s->output),0,s->output.size);
  ret=close(s->fd);
  if(ret<0) { s->poisoned=true; return failure(); } /* never blindly retry close */
  memset(s,0,sizeof(*s)); free(s);
  return 0;
}
int a733_tensor_client_open(const char *package,
                         const struct a733_tensor_client_contract *expected,
                         struct a733_tensor_client **out)
{
  struct a733_tensor_client *s;
  struct a733_tensor_query_s *query;
  uint32_t input_bytes, output_bytes;
  int ret;
  if(!out) return -EINVAL;
  *out=NULL;
  if(!package || !expected || strlen(package)>=A733_TENSOR_PATH) return -EINVAL;
  ret=total(expected->input_bytes,expected->inputs,&input_bytes); if(ret<0) return ret;
  ret=total(expected->output_bytes,expected->outputs,&output_bytes); if(ret<0) return ret;
  if(input_bytes>A733_TENSOR_IO_LIMIT-output_bytes) return -E2BIG;
  s=calloc(1,sizeof(*s)); query=calloc(1,sizeof(*query));
  if(!s || !query) { free(s); free(query); return -ENOMEM; }
  s->fd=open("/dev/npu0",O_RDWR|O_CLOEXEC);
  if(s->fd<0) { ret=failure(); free(query); free(s); return ret; }
  query->version=A733_TENSOR_ABI; strcpy(query->package_path,package);
  if(ioctl(s->fd,A733_NPUIOC_TENSOR_QUERY,(unsigned long)(uintptr_t)query)<0)
    { ret=failure(); goto failed; }
  if(query->status || query->inputs!=expected->inputs || query->outputs!=expected->outputs ||
     memcmp(query->input_bytes,expected->input_bytes,expected->inputs*4u) ||
     memcmp(query->output_bytes,expected->output_bytes,expected->outputs*4u))
    { ret=-EPROTO; goto failed; }
  s->contract=*expected;
  s->input.size=input_bytes; s->input.flags=A733_NPU_BUFFER_READ;
  if(ioctl(s->fd,A733_NPUIOC_ALLOC,(unsigned long)(uintptr_t)&s->input)<0)
    { ret=failure(); s->input.cpu_address=0; goto failed; }
  if(!s->input.handle || !s->input.cpu_address || s->input.allocated>A733_TENSOR_IO_LIMIT ||
     s->input.cpu_address>UINTPTR_MAX-s->input.allocated || s->input.size!=input_bytes || s->input.allocated<input_bytes)
    { s->input.cpu_address=0; ret=-EPROTO; goto failed; }
  s->output.size=output_bytes; s->output.flags=A733_NPU_BUFFER_WRITE;
  if(ioctl(s->fd,A733_NPUIOC_ALLOC,(unsigned long)(uintptr_t)&s->output)<0)
    { ret=failure(); s->output.cpu_address=0; goto failed; }
  if(!s->output.handle || !s->output.cpu_address || s->output.allocated>A733_TENSOR_IO_LIMIT ||
     s->output.cpu_address>UINTPTR_MAX-s->output.allocated || s->output.size!=output_bytes || s->output.allocated<output_bytes)
    { s->output.cpu_address=0; ret=-EPROTO; goto failed; }
  strcpy(s->run.package_path,package);
  free(query); *out=s;
  return 0;
failed:
  free(query);
  /* No RUN has occurred, so the owned fd can reclaim a partial allocation. */
  if(s->input.cpu_address) memset(address(&s->input),0,input_bytes);
  if(s->output.cpu_address) memset(address(&s->output),0,output_bytes);
  close(s->fd); memset(s,0,sizeof(*s)); free(s);
  return ret;
}
static int range(const void *p,size_t n)
{
  return p && n && (uintptr_t)p<=UINTPTR_MAX-n;
}
uint32_t a733_tensor_client_crc32(const void *data,size_t bytes,uint32_t seed)
{
  return a733_tensor_crc32part(data,bytes,seed);
}
int a733_tensor_client_full_clean(struct a733_tensor_client *s,int enable)
{
  if(!s || (enable!=0 && enable!=1)) return -EINVAL;
  if(s->busy) return -EBUSY;
  if(s->poisoned) return -EIO;
  s->full_clean=enable!=0; return 0;
}
int a733_tensor_client_get_profile(struct a733_tensor_client *s,
                                  struct a733_tensor_client_profile *out)
{
  struct a733_tensor_profile_s p={.version=A733_TENSOR_ABI};
  int ret;
  if(!s || !out) return -EINVAL;
  if(s->busy) return -EBUSY;
  if(s->poisoned) return -EIO;
  ret=ioctl(s->fd,A733_NPUIOC_TENSOR_PROFILE,(unsigned long)(uintptr_t)&p);
  if(ret<0) { s->poisoned=true; return failure(); }
  if(p.version!=A733_TENSOR_ABI || p.status || p.committed!=1 || p.reserved)
    { s->poisoned=true; return -EPROTO; }
  _Static_assert(A733_TENSOR_CLIENT_PHASES==A733_TENSOR_PROFILE_PHASES,"profile phases");
  memcpy(out->us,p.us,sizeof(out->us));
  out->clean_bytes=p.clean_bytes;out->table_clean_bytes=p.table_clean_bytes;out->rebuild_bytes=p.rebuild_bytes;
  return 0;
}
int a733_tensor_client_crc_legacy(struct a733_tensor_client *s,int enable)
{
  if(!s || (enable!=0 && enable!=1)) return -EINVAL;
  if(s->busy) return -EBUSY;
  if(s->poisoned) return -EIO;
  s->crc_legacy=enable!=0; return 0;
}
int a733_tensor_client_prepare(struct a733_tensor_client *s)
{
  struct a733_tensor_query_s *q;
  int ret;
  if(!s) return -EINVAL;
  if(s->busy) return -EBUSY;
  if(s->poisoned) return -EIO;
  if(s->resident) return -EALREADY;
  q=calloc(1,sizeof(*q)); if(!q) return -ENOMEM;
  q->version=A733_TENSOR_ABI; strcpy(q->package_path,s->run.package_path);
  s->busy=true;
  ret=ioctl(s->fd,A733_NPUIOC_TENSOR_PREPARE,(unsigned long)(uintptr_t)q);
  if(ret<0) ret=failure();
  else if(q->status || q->inputs!=s->contract.inputs || q->outputs!=s->contract.outputs ||
          memcmp(q->input_bytes,s->contract.input_bytes,s->contract.inputs*4u) ||
          memcmp(q->output_bytes,s->contract.output_bytes,s->contract.outputs*4u)) ret=-EPROTO;
  else { ret=0; s->resident=true; }
  if(ret<0) s->poisoned=true;
  s->busy=false; free(q); return ret;
}
int a733_tensor_client_execute(struct a733_tensor_client *s,
                            const struct a733_tensor_client_input *input,
                            const struct a733_tensor_client_output *output,
                            uint32_t timeout_ms,int verify_capture)
{
  uint32_t i,j,offset;
  int ret;
  if(!s || !input || !output || !timeout_ms || timeout_ms>5000 ||
     (verify_capture!=0 && verify_capture!=1)) return -EINVAL;
  if(s->busy) return -EBUSY;
  if(s->poisoned) return -EIO;
  for(i=0;i<s->contract.inputs;++i)
    if(input[i].bytes!=s->contract.input_bytes[i] || !range(input[i].data,input[i].bytes)) return -EINVAL;
  for(i=0;i<s->contract.outputs;++i) {
    if(output[i].bytes!=s->contract.output_bytes[i] || !range(output[i].data,output[i].bytes)) return -EINVAL;
    for(j=0;j<i;++j)
      if((uintptr_t)output[i].data<(uintptr_t)output[j].data+output[j].bytes &&
         (uintptr_t)output[j].data<(uintptr_t)output[i].data+output[i].bytes) return -EADDRINUSE;
  }
  /* All input validation precedes any device call or application output write. */
  s->busy=true;
  s->run.version=A733_TENSOR_ABI;
  s->run.flags=(verify_capture ? A733_TENSOR_VERIFY_GOLDEN : 0) |
               (s->resident ? A733_TENSOR_RESIDENT : 0) |
               (s->full_clean ? A733_TENSOR_FULL_CLEAN : 0) |
               (s->crc_legacy ? A733_TENSOR_CRC_LEGACY : 0);
  s->run.timeout_ms=timeout_ms;
  s->run.inputs=s->contract.inputs; s->run.outputs=s->contract.outputs;
  s->run.committed=0; s->run.status=-EAGAIN;
  offset=0;
  for(i=0;i<s->contract.inputs;++i) {
    s->run.input[i]=(struct a733_tensor_ref_s){s->input.handle,offset,(uint32_t)input[i].bytes,0};
    memcpy((uint8_t *)address(&s->input)+offset,input[i].data,input[i].bytes);
    offset+=aligned((uint32_t)input[i].bytes);
  }
  offset=0;
  for(i=0;i<s->contract.outputs;++i) {
    s->run.output[i]=(struct a733_tensor_ref_s){s->output.handle,offset,(uint32_t)output[i].bytes,0};
    offset+=aligned((uint32_t)output[i].bytes);
  }
  ret=ioctl(s->fd,A733_NPUIOC_TENSOR_RUN,(unsigned long)(uintptr_t)&s->run);
  if(ret<0 || s->run.status || s->run.hardware_status || s->run.committed!=1 ||
     s->run.inputs!=s->contract.inputs || s->run.outputs!=s->contract.outputs) {
    ret=ret<0 ? failure() : -EPROTO;
    s->poisoned=true; s->busy=false; return ret;
  }
  offset=0;
  for(i=0;i<s->contract.outputs;++i) {
    const struct a733_tensor_ref_s *ref=&s->run.output[i];
    if(ref->handle!=s->output.handle || ref->offset!=offset ||
       ref->bytes!=s->contract.output_bytes[i] || ref->reserved) {
      s->poisoned=true; s->busy=false; return -EPROTO;
    }
    offset+=aligned(ref->bytes);
  }
  for(i=0;i<s->contract.outputs;++i)
    memcpy(output[i].data,(uint8_t *)address(&s->output)+s->run.output[i].offset,output[i].bytes);
  s->busy=false;
  return 0;
}
