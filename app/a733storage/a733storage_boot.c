/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include <system/a733_storage.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <syslog.h>
#include <unistd.h>

static pthread_mutex_t boot_lock=PTHREAD_MUTEX_INITIALIZER;
static bool boot_active,boot_attempted,boot_ready,boot_qualified;
static int boot_error;
static const char *boot_stage="not-started";
bool a733_storage_ready(void)
{
  pthread_mutex_lock(&boot_lock);bool ready=boot_ready;
  pthread_mutex_unlock(&boot_lock);
  struct statfs fs;
  return ready&&statfs("/ufs",&fs)==0&&fs.f_type==MSDOS_SUPER_MAGIC&&fs.f_blocks>0;
}
void a733_storage_status(void)
{
  pthread_mutex_lock(&boot_lock);
  bool active=boot_active,attempted=boot_attempted,ready=boot_ready;
  int error=boot_error;const char *stage=boot_stage;
  pthread_mutex_unlock(&boot_lock);
  ready=ready&&a733_storage_ready();
  printf("ufs-mount: active=%u attempted=%u ready=%u error=%d stage=%s mount=/ufs source=/dev/ufsdata fallback=/data/models\n",
         active,attempted,ready,error,stage);
}
static int command(const char *text)
{
  pthread_mutex_lock(&boot_lock);boot_stage=text;pthread_mutex_unlock(&boot_lock);
  int fd=open("/dev/a733-ufs",O_WRONLY);if(fd<0)return -errno;
  size_t n=strlen(text);ssize_t written=write(fd,text,n);
  int ret=written==(ssize_t)n?0:written<0?-errno:-EIO;
  if(close(fd)&&!ret)ret=-errno;
  return ret;
}
static int boot_worker(int argc,char **argv)
{
  (void)argc;(void)argv;int ret=0;
  /* One attempt, no scratch WRITE, no benchmark, reset, retry or mkfatfs.
   * Existing-FAT qualification is in the kernel, not a forged scratch flag. */
  static const char *const steps[]={"board","handoff","quiesce","dma-nop",
                                   "readcheck","block-ro","data-existing"};
  pthread_mutex_lock(&boot_lock);bool qualified=boot_qualified;pthread_mutex_unlock(&boot_lock);
  if(!qualified)
    for(unsigned i=0;i<sizeof(steps)/sizeof(steps[0])&&!ret;i++)ret=command(steps[i]);
  if(!ret){pthread_mutex_lock(&boot_lock);boot_qualified=true;boot_stage="mounting";
    pthread_mutex_unlock(&boot_lock);}
  if(!ret&&mkdir("/ufs",0755)<0&&errno!=EEXIST)ret=-errno;
  if(!ret&&mount("/dev/ufsdata","/ufs","vfat",0,NULL)<0)ret=-errno;
  pthread_mutex_lock(&boot_lock);boot_active=false;boot_error=ret;
  if(!ret){boot_ready=true;boot_stage="mounted-existing-FAT";}
  /* Never reinterpret EBUSY as success: it could be an unrelated mount. */
  const char *stage=boot_stage;pthread_mutex_unlock(&boot_lock);
  syslog(ret?LOG_ERR:LOG_NOTICE,"UFS startup: stage=%s result=%d ready=%u; no format, SD retained\n",stage,ret,!ret);
  return ret?1:0;
}
int a733_storage_start(bool background)
{
  pthread_mutex_lock(&boot_lock);
  bool remount=boot_qualified&&!boot_active&&!boot_ready&&!strcmp(boot_stage,"unmounted");
  if(boot_attempted&&!remount){int ret=boot_ready?0:boot_active?-EBUSY:boot_error?boot_error:-EALREADY;
    pthread_mutex_unlock(&boot_lock);return ret;}
  boot_attempted=boot_active=true;boot_stage="starting";
  pthread_mutex_unlock(&boot_lock);
  if(!background)return boot_worker(0,NULL)?-EIO:0;
  int pid=task_create("ufs-mount",100,8192,boot_worker,NULL);
  if(pid<0){int ret=-errno;pthread_mutex_lock(&boot_lock);
    boot_active=false;boot_error=ret;boot_stage="spawn-failed";
    pthread_mutex_unlock(&boot_lock);return ret;}
  return 0;
}
int a733_storage_unmount(void)
{
  pthread_mutex_lock(&boot_lock);
  if(!boot_ready||boot_active){pthread_mutex_unlock(&boot_lock);return -EACCES;}
  boot_active=true;pthread_mutex_unlock(&boot_lock);
  int ret=umount("/ufs")<0?-errno:0;
  pthread_mutex_lock(&boot_lock);boot_active=false;
  if(!ret){boot_ready=false;boot_stage="unmounted";boot_error=0;}
  pthread_mutex_unlock(&boot_lock);return ret;
}
