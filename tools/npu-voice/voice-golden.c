/* SPDX-License-Identifier: Apache-2.0 */
/* Linux reference ONLY. Uses authorized vendor libraries, never an RTOS shim.
 * A raw tensor fixture is not a WAV; caller supplies every padded tensor.
 */
#include "vip_lite.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define MAX_IO 64u
#define MAX_BYTES (128u * 1024u * 1024u)
struct tensor { vip_buffer handle; vip_uint32_t bytes; };
static struct tensor inputs[MAX_IO], outputs[MAX_IO];

static uint32_t crc(const unsigned char *p, size_t bytes)
{
  uint32_t value=0;
  while (bytes--) {
    value^=*p++;
    for (unsigned b=0;b<8;b++) value=(value>>1)^(0xedb88320u & (0u-(value&1u)));
  }
  return value;
}

static int tensor_info(vip_network net, unsigned index, int output,
                       struct tensor *t, uint64_t *total)
{
  vip_buffer_create_params_t p={0};
  char name[256]={0};
  p.memory_type=VIP_BUFFER_MEMORY_TYPE_DEFAULT;
#define QUERY(prop,dest) do { \
  vip_status_e s=output ? vip_query_output(net,index,prop,dest) : \
                         vip_query_input(net,index,prop,dest); \
  if (s!=VIP_SUCCESS) { fprintf(stderr,"tensor query %u/%u prop=%d rc=%d\n", \
    output,index,(int)prop,s); return -1; } \
} while (0)
  QUERY(VIP_BUFFER_PROP_DATA_FORMAT,&p.data_format);
  QUERY(VIP_BUFFER_PROP_NUM_OF_DIMENSION,&p.num_of_dims);
  if (!p.num_of_dims || p.num_of_dims>6) return -1;
  QUERY(VIP_BUFFER_PROP_SIZES_OF_DIMENSION,p.sizes);
  QUERY(VIP_BUFFER_PROP_QUANT_FORMAT,&p.quant_format);
  QUERY(VIP_BUFFER_PROP_NAME,name);
  if (!memchr(name,0,sizeof(name))) return -1;
  if (p.quant_format==VIP_BUFFER_QUANTIZE_DYNAMIC_FIXED_POINT)
    QUERY(VIP_BUFFER_PROP_FIXED_POINT_POS,&p.quant_data.dfp.fixed_point_pos);
  else if (p.quant_format==VIP_BUFFER_QUANTIZE_TF_ASYMM) {
    QUERY(VIP_BUFFER_PROP_TF_SCALE,&p.quant_data.affine.scale);
    QUERY(VIP_BUFFER_PROP_TF_ZERO_POINT,&p.quant_data.affine.zeroPoint);
  }
#undef QUERY
  uint64_t elements=1;
  for (unsigned d=0;d<p.num_of_dims;d++) {
    if (!p.sizes[d] || elements>MAX_BYTES/p.sizes[d]) return -1;
    elements*=p.sizes[d];
  }
  if (elements>MAX_BYTES/8) return -1; /* conservative max element width */
  if (vip_create_buffer(&p,sizeof(p),&t->handle)!=VIP_SUCCESS) return -1;
  t->bytes=vip_get_buffer_size(t->handle);
  if (!t->bytes || t->bytes>MAX_BYTES || *total>MAX_BYTES-t->bytes) return -1;
  *total+=t->bytes;
  printf("%s[%u] name=%s padded-bytes=%u format=%d quant=%d dims=",
         output?"output":"input",index,name,t->bytes,p.data_format,p.quant_format);
  for (unsigned d=0;d<p.num_of_dims;d++) printf("%s%u",d?"x":"",p.sizes[d]);
  if (p.quant_format==VIP_BUFFER_QUANTIZE_DYNAMIC_FIXED_POINT)
    printf(" dfp=%d",p.quant_data.dfp.fixed_point_pos);
  else if (p.quant_format==VIP_BUFFER_QUANTIZE_TF_ASYMM)
    printf(" scale=%.9g zero=%d",p.quant_data.affine.scale,p.quant_data.affine.zeroPoint);
  putchar('\n');
  return 0;
}

static int input_tensor(struct tensor *t, const char *directory, unsigned index)
{
  void *memory=vip_map_buffer(t->handle);
  if (!memory) return -1;
  int result=-1;
  if (!strcmp(directory,"--zero")) memset(memory,0,t->bytes);
  else {
    char path[1024];
    if (snprintf(path,sizeof(path),"%s/input%u.bin",directory,index)>=(int)sizeof(path))
      goto done;
    int fd=open(path,O_RDONLY|O_NOFOLLOW);
    if (fd<0) { perror(path); goto done; }
    struct stat st;
    if (fstat(fd,&st)<0 || !S_ISREG(st.st_mode) || st.st_size!=t->bytes) {
      fprintf(stderr,"input%u must be exactly %u padded bytes\n",index,t->bytes);
      close(fd); goto done;
    }
    size_t offset=0;
    while (offset<t->bytes) {
      ssize_t n=read(fd,(unsigned char *)memory+offset,t->bytes-offset);
      if (n<0 && errno==EINTR) continue;
      if (n<=0) break;
      offset+=(size_t)n;
    }
    int closed=close(fd);
    if (offset!=t->bytes || closed<0) goto done;
  }
  printf("input[%u] bytes=%u raw-crc32=%08" PRIx32 "\n",index,t->bytes,
         crc(memory,t->bytes));
#ifdef VOICE_CAPTURE_LOGICAL
  printf("native-buffer input index=%u logical=%016" PRIxPTR " bytes=%u\n",
         index,(uintptr_t)memory,t->bytes);
#endif
  if (vip_flush_buffer(t->handle,VIP_BUFFER_OPER_TYPE_FLUSH)!=VIP_SUCCESS) goto done;
  result=0;
done:
  if (vip_unmap_buffer(t->handle)!=VIP_SUCCESS) result=-1;
  return result;
}

static int output_tensor(struct tensor *t, const char *dir, unsigned index)
{
  if (vip_flush_buffer(t->handle,VIP_BUFFER_OPER_TYPE_INVALIDATE)!=VIP_SUCCESS) return -1;
  const unsigned char *p=vip_map_buffer(t->handle);
  if (!p) return -1;
#ifdef VOICE_CAPTURE_LOGICAL
  printf("native-buffer output index=%u logical=%016" PRIxPTR " bytes=%u\n",
         index,(uintptr_t)p,t->bytes);
#endif
  char path[1024];
  int result=-1;
  if (snprintf(path,sizeof(path),"%s/output%u.bin",dir,index)>=(int)sizeof(path)) goto done;
  int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
  if (fd<0) { perror(path); goto done; }
  size_t offset=0, changed=0;
  for (size_t j=0;j<t->bytes;j++) changed+=p[j]!=0xa5;
  while (offset<t->bytes) {
    ssize_t n=write(fd,p+offset,t->bytes-offset);
    if (n<0 && errno==EINTR) continue;
    if (n<=0) break;
    offset+=(size_t)n;
  }
  int synced=fsync(fd), closed=close(fd);
  if (offset==t->bytes && synced==0 && closed==0) result=0;
  printf("output[%u] bytes=%u raw-crc32=%08" PRIx32 " changed=%zu status=%d\n",
         index,t->bytes,crc(p,t->bytes),changed,result);
done:
  if (vip_unmap_buffer(t->handle)!=VIP_SUCCESS) result=-1;
  return result;
}

int main(int argc, char **argv)
{
  vip_network net=NULL;
  vip_uint32_t ni=0,no=0,cid=0,timeout=5000;
  uint64_t total=0;
  int initialized=0,prepared=0,result=1;
  unsigned repeat=1;
  memset(inputs,0,sizeof(inputs)); memset(outputs,0,sizeof(outputs));
  if (!((argc==3 && !strcmp(argv[2],"--inspect")) || argc==4 ||
        (argc==6 && !strcmp(argv[4],"--repeat")))) {
    fprintf(stderr,"Linux only: %s MODEL.nb --inspect\n"
      "            %s MODEL.nb INPUT_DIR|--zero NEW_OUTPUT_DIR [--repeat 1..20]\n",argv[0],argv[0]);
    return 2;
  }
  if (argc==6) {
    char *end=NULL; errno=0;
    unsigned long parsed=strtoul(argv[5],&end,10);
    if(errno || !end || *end || parsed<1 || parsed>20) return 2;
    repeat=(unsigned)parsed;
  }
  setvbuf(stdout,NULL,_IOLBF,0);
  printf("REFERENCE ONLY VIPLite=%08x; not recognition or RTOS acceptance\n",vip_get_version());
  if (vip_init()!=VIP_SUCCESS) goto done;
  initialized=1;
  if (vip_query_hardware(VIP_QUERY_HW_PROP_CID,sizeof(cid),&cid)!=VIP_SUCCESS || cid!=0x1000003b)
    { fprintf(stderr,"wrong hardware CID=%08x\n",cid); goto done; }
  if (vip_create_network(argv[1],0,VIP_CREATE_NETWORK_FROM_FILE,&net)!=VIP_SUCCESS) goto done;
  if (vip_query_network(net,VIP_NETWORK_PROP_INPUT_COUNT,&ni)!=VIP_SUCCESS ||
      vip_query_network(net,VIP_NETWORK_PROP_OUTPUT_COUNT,&no)!=VIP_SUCCESS ||
      !ni || !no || ni>MAX_IO || no>MAX_IO) goto done;
  printf("inputs=%u outputs=%u\n",ni,no);
  for (unsigned i=0;i<ni;i++) if (tensor_info(net,i,0,&inputs[i],&total)) goto done;
  for (unsigned i=0;i<no;i++) if (tensor_info(net,i,1,&outputs[i],&total)) goto done;
  if (argc==3) { result=0; goto done; }
  /* Refuse existing output dirs, so a stale prefix cannot look successful. */
  if (mkdir(argv[3],0700)<0) { perror(argv[3]); goto done; }
  if (vip_set_network(net,VIP_NETWORK_PROP_SET_TIME_OUT,&timeout)!=VIP_SUCCESS ||
      vip_prepare_network(net)!=VIP_SUCCESS) goto done;
  prepared=1;
  for (unsigned i=0;i<ni;i++) {
    if (input_tensor(&inputs[i],argv[2],i) ||
        vip_set_input(net,i,inputs[i].handle)!=VIP_SUCCESS) goto done;
  }
  for (unsigned i=0;i<no;i++) {
    void *memory=vip_map_buffer(outputs[i].handle);
    if (!memory) goto done;
    memset(memory,0xa5,outputs[i].bytes);
    vip_status_e flushed=vip_flush_buffer(outputs[i].handle,VIP_BUFFER_OPER_TYPE_FLUSH);
    vip_status_e unmapped=vip_unmap_buffer(outputs[i].handle);
    if (flushed!=VIP_SUCCESS || unmapped!=VIP_SUCCESS ||
        vip_set_output(net,i,outputs[i].handle)!=VIP_SUCCESS) goto done;
  }
  for(unsigned iteration=0;iteration<repeat;iteration++) {
  char step[1024]; const char *directory=argv[3];
  if(repeat>1) {
    if(snprintf(step,sizeof(step),"%s/step%03u",argv[3],iteration)>=(int)sizeof(step) ||
       mkdir(step,0700)<0) goto done;
    directory=step;
    printf("repeat-index=%u total=%u\n",iteration,repeat);
  }
  if(iteration) for(unsigned i=0;i<no;i++) {
    void *p=vip_map_buffer(outputs[i].handle);
    if(!p) goto done;
    memset(p,0xa5,outputs[i].bytes);
    vip_status_e a=vip_flush_buffer(outputs[i].handle,VIP_BUFFER_OPER_TYPE_FLUSH);
    vip_status_e b=vip_unmap_buffer(outputs[i].handle);
    if(a!=VIP_SUCCESS || b!=VIP_SUCCESS) goto done;
  }
  struct timespec begin,end;
  if (clock_gettime(CLOCK_MONOTONIC,&begin)<0) goto done;
  vip_status_e status=vip_run_network(net);
  if (clock_gettime(CLOCK_MONOTONIC,&end)<0) goto done;
  printf("run-status=%d wall-us=%" PRId64 "\n",status,
    ((int64_t)end.tv_sec-begin.tv_sec)*1000000+(end.tv_nsec-begin.tv_nsec)/1000);
  if (status!=VIP_SUCCESS) goto done;
  for (unsigned i=0;i<no;i++) if (output_tensor(&outputs[i],directory,i)) goto done;
  }
  result=0;
done:
  if (net && prepared && vip_finish_network(net)!=VIP_SUCCESS) result=1;
  for (unsigned i=0;i<MAX_IO;i++) {
    if (inputs[i].handle && vip_destroy_buffer(inputs[i].handle)!=VIP_SUCCESS) result=1;
    if (outputs[i].handle && vip_destroy_buffer(outputs[i].handle)!=VIP_SUCCESS) result=1;
  }
  if (net && vip_destroy_network(net)!=VIP_SUCCESS) result=1;
  if (initialized && vip_destroy()!=VIP_SUCCESS) result=1;
  printf("reference-status=%d; verify this exit status and every output hash\n",result);
  return result;
}
