/* SPDX-License-Identifier: Apache-2.0 */
/* File-level, explicit jobs only. Keep the shell alive during bulk I/O.
 * Stop/deadline are observed BETWEEN I/O calls, never by killing a task that
 * owns a DMA transaction or FAT lock. A blocked kernel call is not forcibly
 * interrupted. Partial destinations are retained and NEVER called verified.
 */
#include <nuttx/config.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <mbedtls/sha256.h>
#ifdef CONFIG_SYSTEM_A733STORAGE
#include <system/a733_storage.h>
#include <system/a733_services.h>
#endif

#define BULK_BYTES (256u * 1024u)
#define JOB_LIMIT_MS UINT64_C(900000)
#define PATH_BYTES 512
enum operation { NONE, COPY, COMPARE, READBENCH };
struct storage_job
{
  bool active, verified, created;
  enum operation op;
  int pid, result, close_error;
  uint64_t total, done, checked, start_ms, finish_ms, progress_ms, report_ms, limit;
  const char *phase;
  char source[PATH_BYTES], target[PATH_BYTES];
};
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct storage_job g_job = {.phase="idle"};
static atomic_bool g_stop = ATOMIC_VAR_INIT(false);

static uint64_t now_ms(void)
{
  struct timespec ts;
  if(clock_gettime(CLOCK_MONOTONIC,&ts))return 0;
  return (uint64_t)ts.tv_sec*1000+(uint64_t)ts.tv_nsec/1000000;
}
static void on_interrupt(int signo)
{
  (void)signo;atomic_store_explicit(&g_stop,true,memory_order_relaxed);
}
static int checkpoint(void)
{
  if(atomic_load_explicit(&g_stop,memory_order_relaxed))return -ECANCELED;
  if(now_ms()-g_job.start_ms>JOB_LIMIT_MS)return -ETIMEDOUT;
  return 0;
}
static void phase(const char *name)
{
  pthread_mutex_lock(&g_lock);g_job.phase=name;pthread_mutex_unlock(&g_lock);
}
static void print_status(const struct storage_job *snapshot)
{
  struct storage_job view=*snapshot;
  uint64_t end=view.finish_ms?view.finish_ms:now_ms();
  uint64_t elapsed=view.start_ms?end-view.start_ms:0;
  uint64_t age=view.progress_ms?end-view.progress_ms:0;
  printf("ufs: active=%u pid=%d phase=%s result=%d verified=%u created=%u close-error=%d stop=%u "
    "bytes=%" PRIu64 "/%" PRIu64 " checked=%" PRIu64 " elapsed-ms=%" PRIu64 " no-progress-ms=%" PRIu64 "\n",
    view.active,view.pid,view.phase,view.result,view.verified,view.created,view.close_error,
    atomic_load_explicit(&g_stop,memory_order_relaxed),view.done,view.total,view.checked,elapsed,age);
}
static void status(void)
{
  struct storage_job view;
  pthread_mutex_lock(&g_lock);view=g_job;pthread_mutex_unlock(&g_lock);
  print_status(&view);
#ifdef CONFIG_SYSTEM_A733STORAGE
  a733_storage_status();
#endif
}
static void progress(uint64_t done,uint64_t checked,bool force)
{
  uint64_t now=now_ms();bool emit;
  pthread_mutex_lock(&g_lock);
  if(g_job.done!=done||g_job.checked!=checked)g_job.progress_ms=now;
  g_job.done=done;g_job.checked=checked;
  emit=force||now-g_job.report_ms>=2000;
  if(emit)g_job.report_ms=now;
  pthread_mutex_unlock(&g_lock);
  if(emit)status();
  sched_yield();
}
static bool path_ok(const char *p)
{
  if(!p||strlen(p)>=PATH_BYTES||
     (strncmp(p,"/data/",6)&&strncmp(p,"/ufs/",5)))return false;
  for(const char *s=p;*s;)
    {
      if(*s=='/'){++s;continue;}
      const char *end=strchr(s,'/');size_t n=end?(size_t)(end-s):strlen(s);
      if((n==1&&s[0]=='.')||(n==2&&!memcmp(s,"..",2)))return false;
      s+=n;
    }
  return p[strlen(p)-1]!='/';
}
static int regular(int fd,uint64_t *bytes)
{
  struct stat st;
  if(fstat(fd,&st))return -errno;
  if(!S_ISREG(st.st_mode)||st.st_size<0)return -EINVAL;
  *bytes=(uint64_t)st.st_size;return 0;
}
static int read_all(int fd,uint8_t *p,size_t count)
{
  unsigned interrupts=0;
  while(count)
    {
      int ret=checkpoint();if(ret)return ret;
      ssize_t n=read(fd,p,count);
      if(n<0){if(errno==EINTR&&++interrupts<16)continue;return -errno;}
      if(n==0)return -EIO; /* Known size, so premature EOF is a failure. */
      p+=n;count-=(size_t)n;
    }
  return 0;
}
static int write_all(int fd,const uint8_t *p,size_t count)
{
  unsigned interrupts=0;
  while(count)
    {
      int ret=checkpoint();if(ret)return ret;
      ssize_t n=write(fd,p,count);
      if(n<0){if(errno==EINTR&&++interrupts<16)continue;return -errno;}
      if(n==0)return -EIO; /* Unlike old NSH cp: zero progress NEVER loops. */
      p+=n;count-=(size_t)n;
    }
  return 0;
}
static int hash_file(int fd,uint8_t *buffer,uint64_t bytes,uint8_t digest[32])
{
  mbedtls_sha256_context hash;mbedtls_sha256_init(&hash);
  int ret=mbedtls_sha256_starts(&hash,0)?-EIO:0;
  for(uint64_t offset=0;!ret&&offset<bytes;)
    {
      size_t n=bytes-offset>BULK_BYTES?BULK_BYTES:(size_t)(bytes-offset);
      ret=read_all(fd,buffer,n);
      if(!ret&&mbedtls_sha256_update(&hash,buffer,n))ret=-EIO;
      if(!ret){offset+=n;progress(bytes,offset,false);}
    }
  if(!ret&&mbedtls_sha256_finish(&hash,digest))ret=-EIO;
  mbedtls_sha256_free(&hash);return ret;
}
static int copy_file(int input,uint8_t *buffer,uint64_t bytes)
{
  struct statfs fs;
  if(statfs("/ufs",&fs))return -errno;
  if(fs.f_type!=MSDOS_SUPER_MAGIC)return -ENODEV;
  /* Explicit NEW regular file only. No raw devices, overwrite, unlink or
   * rename. A failed/cancelled file remains for diagnosis; use a NEW name. */
  int output=open(g_job.target,O_WRONLY|O_CREAT|O_EXCL,0600);
  if(output<0)return -errno;
  pthread_mutex_lock(&g_lock);g_job.created=true;pthread_mutex_unlock(&g_lock);
  mbedtls_sha256_context hash;mbedtls_sha256_init(&hash);
  uint8_t expected[32],actual[32];
  int ret=mbedtls_sha256_starts(&hash,0)?-EIO:0;
  phase("copy-256KiB");
  for(uint64_t offset=0;!ret&&offset<bytes;)
    {
      size_t n=bytes-offset>BULK_BYTES?BULK_BYTES:(size_t)(bytes-offset);
      ret=read_all(input,buffer,n);
      if(!ret&&mbedtls_sha256_update(&hash,buffer,n))ret=-EIO;
      if(!ret)ret=write_all(output,buffer,n);
      if(!ret){offset+=n;progress(offset,0,false);}
    }
  if(!ret&&mbedtls_sha256_finish(&hash,expected))ret=-EIO;
  mbedtls_sha256_free(&hash);
  /* Even cancellation closes normally: never kill a lock/DMA owner. */
  phase("sync-close");
  int cleanup=fsync(output)?-errno:0;
  if(close(output)&&!cleanup)cleanup=-errno;
  pthread_mutex_lock(&g_lock);g_job.close_error=cleanup;pthread_mutex_unlock(&g_lock);
  if(!ret)ret=cleanup;
  if(ret)return ret;
  ret=checkpoint();if(ret)return ret;
  phase("verify-SHA256");
  output=open(g_job.target,O_RDONLY);if(output<0)return -errno;
  uint64_t actual_size=0;ret=regular(output,&actual_size);
  if(!ret&&actual_size!=bytes)ret=-EBADMSG;
  if(!ret)ret=hash_file(output,buffer,bytes,actual);
  if(close(output)&&!ret)ret=-errno;
  if(!ret&&memcmp(expected,actual,32))ret=-EBADMSG;
  if(!ret)
    {
      char hex[65];
      for(unsigned i=0;i<32;i++)snprintf(hex+2*i,3,"%02x",actual[i]);
      printf("ufs: SHA256=%s (destination matches copied source stream)\n",hex);
    }
  return ret;
}
static int compare_files(int input,uint8_t *first,uint64_t bytes)
{
  int second=open(g_job.target,O_RDONLY);if(second<0)return -errno;
  uint8_t *other=malloc(BULK_BYTES);uint64_t actual_size=0;
  int ret=other?regular(second,&actual_size):-ENOMEM;
  if(!ret&&actual_size!=bytes)
    {printf("ufs: sizes differ source=%" PRIu64 " target=%" PRIu64 "\n",bytes,actual_size);ret=-EBADMSG;}
  phase("compare-256KiB");
  for(uint64_t offset=0;!ret&&offset<bytes;)
    {
      size_t n=bytes-offset>BULK_BYTES?BULK_BYTES:(size_t)(bytes-offset);
      ret=read_all(input,first,n);if(!ret)ret=read_all(second,other,n);
      if(!ret&&memcmp(first,other,n))
        {
          size_t i=0;while(i<n&&first[i]==other[i])++i;
          printf("ufs: first mismatch at byte=%" PRIu64 "\n",offset+i);ret=-EBADMSG;
        }
      if(!ret){offset+=n;progress(offset,offset,false);}
    }
  free(other);if(close(second)&&!ret)ret=-errno;return ret;
}
static int worker(int argc,char **argv)
{
  (void)argc;(void)argv;
  /* Serialize startup with the parent's PID publication. */
  pthread_mutex_lock(&g_lock);pthread_mutex_unlock(&g_lock);
  struct sigaction action;memset(&action,0,sizeof(action));
  action.sa_handler=on_interrupt;sigemptyset(&action.sa_mask);
  int ret=sigaction(SIGINT,&action,NULL)?-errno:0;
  int input=-1;uint8_t *buffer=NULL;uint64_t bytes=0;
  phase("opening");
  if(!ret){input=open(g_job.source,O_RDONLY);if(input<0)ret=-errno;}
  if(!ret)ret=regular(input,&bytes);
  if(!ret&&g_job.op==COPY&&bytes>=UINT64_C(4294967296))ret=-EFBIG;
  if(!ret&&g_job.op==READBENCH&&bytes>g_job.limit)bytes=g_job.limit;
  pthread_mutex_lock(&g_lock);g_job.total=bytes;pthread_mutex_unlock(&g_lock);
  if(!ret){buffer=malloc(BULK_BYTES);if(!buffer)ret=-ENOMEM;}
  if(!ret)
    {
      progress(0,0,true);
      if(g_job.op==COPY)ret=copy_file(input,buffer,bytes);
      else if(g_job.op==COMPARE)ret=compare_files(input,buffer,bytes);
      else
        {
          phase("readbench-256KiB");uint64_t began=now_ms();
          for(uint64_t offset=0;!ret&&offset<bytes;)
            {
              size_t n=bytes-offset>BULK_BYTES?BULK_BYTES:(size_t)(bytes-offset);
              ret=read_all(input,buffer,n);
              if(!ret){offset+=n;progress(offset,0,false);}
            }
          uint64_t elapsed=now_ms()-began;
          printf("ufs: readbench bytes=%" PRIu64 " elapsed-ms=%" PRIu64 " KiB/s=%" PRIu64 " (file I/O, not inference)\n",
            g_job.done,elapsed,elapsed?g_job.done*1000/1024/elapsed:0);
        }
    }
  free(buffer);if(input>=0&&close(input)&&!ret)ret=-errno;
  if(!ret)ret=checkpoint();
  struct storage_job finished;
  pthread_mutex_lock(&g_lock);g_job.result=ret;g_job.active=false;
  g_job.finish_ms=now_ms();
  g_job.verified=!ret&&(g_job.op==COPY||g_job.op==COMPARE);
  g_job.phase=ret==0?"complete":ret==-ECANCELED?"cancelled":ret==-ETIMEDOUT?"deadline":"failed";
  finished=g_job;
  pthread_mutex_unlock(&g_lock);print_status(&finished);
  if(ret&&finished.created)puts("ufs: incomplete destination retained; NEVER load it as a verified model");
  return ret?1:0;
}
static void usage(void)
{
  puts("ufs status | stop\n"
       "ufs start | unmount | autostart on|off|status\n"
       "ufs copy <source-regular-file> </ufs/NEW-destination>\n"
       "ufs compare <source> <target>\n"
       "ufs readbench <regular-file> [MiB:1..256, default32]\n"
       "Jobs run in background; serial progress/status/stop require NO network.\n"
       "Copy uses O_EXCL (NO overwrite), sync/close and full destination SHA256.\n"
       "Stop is cooperative between I/O calls, not forced cancellation of a blocked driver.\n"
       "No format, deletion, rename, reset, automatic mount or model-path changes.");
}
int main(int argc,char **argv)
{
#ifdef CONFIG_SYSTEM_A733STORAGE
  if(argc==3&&!strcmp(argv[1],"autostart"))return a733_auto_option("ufs",argv[2])?1:0;
  if(argc==2&&(!strcmp(argv[1],"start")||!strcmp(argv[1],"--boot")))
    return a733_storage_start(strcmp(argv[1],"--boot")!=0)?1:0;
  if(argc==2&&!strcmp(argv[1],"unmount"))
    {
      pthread_mutex_lock(&g_lock);bool busy=g_job.active;pthread_mutex_unlock(&g_lock);
      int ret=busy?-EBUSY:a733_storage_unmount();
      printf("ufs: unmount result=%d\n",ret);return ret?1:0;
    }
#endif
  if(argc==2&&!strcmp(argv[1],"status")){status();return 0;}
  if(argc==2&&!strcmp(argv[1],"stop"))
    {
      pthread_mutex_lock(&g_lock);bool active=g_job.active;
      if(active)atomic_store_explicit(&g_stop,true,memory_order_relaxed);
      pthread_mutex_unlock(&g_lock);
      puts(active?"ufs: stop requested; waiting for current I/O then normal sync/close":"ufs: no active job");return 0;
    }
  enum operation op=NONE;uint64_t limit=UINT64_C(32)*1048576;
  if(argc==4&&!strcmp(argv[1],"copy"))op=COPY;
  else if(argc==4&&!strcmp(argv[1],"compare"))op=COMPARE;
  else if((argc==3||argc==4)&&!strcmp(argv[1],"readbench"))
    {
      op=READBENCH;
      if(argc==4)
        {
          char *end;errno=0;unsigned long mib=strtoul(argv[3],&end,10);
          if(errno||!*argv[3]||*end||mib<1||mib>256)return 1;
          limit=(uint64_t)mib*1048576;
        }
    }
  if(op==NONE||!path_ok(argv[2])||
     ((op==COPY||op==COMPARE)&&!path_ok(argv[3]))||
     (op==COPY&&(strncmp(argv[3],"/ufs/",5)||!strcmp(argv[2],argv[3]))))
    {usage();return 1;}
  pthread_mutex_lock(&g_lock);
  if(g_job.active){pthread_mutex_unlock(&g_lock);puts("ufs: busy; use status/stop");return 1;}
  memset(&g_job,0,sizeof(g_job));g_job.active=true;g_job.op=op;
  g_job.phase="queued";g_job.start_ms=g_job.progress_ms=now_ms();g_job.limit=limit;
  memcpy(g_job.source,argv[2],strlen(argv[2])+1);
  if(op!=READBENCH)memcpy(g_job.target,argv[3],strlen(argv[3])+1);
  atomic_store_explicit(&g_stop,false,memory_order_relaxed);
  int pid=task_create("ufs-io",100,8192,worker,NULL);
  if(pid<0){g_job.active=false;g_job.result=-errno;g_job.phase="spawn-failed";}
  else g_job.pid=pid;
  pthread_mutex_unlock(&g_lock);
  if(pid<0){status();return 1;}
  printf("ufs: job accepted pid=%d block=%u; shell available, use 'ufs status' / 'ufs stop'\n",pid,BULK_BYTES);
  return 0;
}
