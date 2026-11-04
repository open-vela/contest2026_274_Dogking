/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <syslog.h>
#include <unistd.h>
static int step,fail_step=-1,mounts,unmounts,fail_mount,fail_unmount,fail_spawn;
static bool mounted;
static int (*pending)(int,char **);
static const char *const expected[]={"board","handoff","quiesce","dma-nop","readcheck","block-ro","data-existing"};
static int fake_open(const char *p,int flags)
{assert(!strcmp(p,"/dev/a733-ufs")&&flags==O_WRONLY);return 88;}
static ssize_t fake_write(int fd,const void *p,size_t n)
{assert(fd==88&&step<7&&strlen(expected[step])==n&&!memcmp(p,expected[step],n));
 if(step++==fail_step){errno=EIO;return -1;}return (ssize_t)n;}
static int fake_close(int fd){assert(fd==88);return 0;}
static int fake_mkdir(const char *p,mode_t m){assert(!strcmp(p,"/ufs")&&m==0755);return 0;}
static int fake_mount(const char *a,const char *b,const char *c,unsigned long flags,const void *d)
{assert(!strcmp(a,"/dev/ufsdata")&&!strcmp(b,"/ufs")&&!strcmp(c,"vfat")&&!flags&&!d);
 ++mounts;if(fail_mount){errno=EBUSY;return -1;}mounted=true;return 0;}
static int fake_umount(const char *p)
{assert(!strcmp(p,"/ufs"));++unmounts;if(fail_unmount){errno=EBUSY;return -1;}mounted=false;return 0;}
static int fake_statfs(const char *p,struct statfs *f)
{assert(!strcmp(p,"/ufs"));memset(f,0,sizeof(*f));f->f_type=mounted?0x4d44:0;f->f_blocks=mounted?30500000:0;return 0;}
static int fake_task(const char *n,int p,int s,int (*fn)(int,char **),char **args)
{assert(!strcmp(n,"ufs-mount")&&p==100&&s==8192&&!args);if(fail_spawn){errno=ENOMEM;return -1;}pending=fn;return 42;}
static void fake_log(int level,const char *format,...)
{(void)level;va_list a;va_start(a,format);vprintf(format,a);va_end(a);}
#define MSDOS_SUPER_MAGIC 0x4d44
#define open fake_open
#define write fake_write
#define close fake_close
#define mkdir fake_mkdir
#define mount fake_mount
#define umount fake_umount
#define statfs(...) fake_statfs(__VA_ARGS__)
#define task_create fake_task
#define syslog fake_log
#include "../app/a733storage/a733storage_boot.c"
static void reset(void)
{boot_active=boot_attempted=boot_ready=boot_qualified=mounted=false;boot_error=0;boot_stage="not-started";
 step=mounts=unmounts=fail_mount=fail_unmount=fail_spawn=0;fail_step=-1;pending=NULL;}
int main(void)
{
 for(int bad=0;bad<7;bad++)
   {reset();fail_step=bad;assert(a733_storage_start(false)<0);
    assert(!a733_storage_ready()&&step==bad+1&&!mounts&&!boot_active);
    assert(a733_storage_start(false)<0&&step==bad+1);}
 reset();fail_mount=1;assert(a733_storage_start(false)<0&&!a733_storage_ready()&&mounts==1);
 reset();assert(!a733_storage_start(false)&&a733_storage_ready()&&step==7&&mounts==1);
 assert(!a733_storage_start(true)&&step==7&&mounts==1); /* idempotent */
 fail_unmount=1;assert(a733_storage_unmount()<0&&a733_storage_ready());
 fail_unmount=0;assert(!a733_storage_unmount()&&!a733_storage_ready());
 assert(!a733_storage_start(false)&&a733_storage_ready()&&step==7&&mounts==2);
 reset();assert(!a733_storage_start(true)&&pending&&boot_active&&!boot_ready);
 assert(a733_storage_start(true)==-EBUSY);pending(0,NULL);assert(a733_storage_ready());
 mounted=false;assert(!a733_storage_ready()); /* external unmount is not stale success */
 reset();fail_spawn=1;assert(a733_storage_start(true)==-ENOMEM&&!boot_active);
 a733_storage_status();puts("Production UFS boot PASS: 7 gate failures/stop-on-error/no scratch or format/mount fail/idempotence/background/unmount/spawn failure; no hardware");
}
