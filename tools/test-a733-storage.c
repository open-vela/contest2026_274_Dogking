/* SPDX-License-Identifier: Apache-2.0 */
/* Compile the actual application with real mbedTLS SHA256 and regular-file
 * POSIX I/O. Only path translation, task spawn, filesystem type and explicit
 * fault injection are mocked; NEVER access a block device. */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <unistd.h>
static char root[512];
static int fault, calls, (*pending)(int,char **);
static int fake_task(const char *n,int p,int s,int (*fn)(int,char **),char **args)
{(void)n;(void)p;(void)s;(void)args;assert(!pending);pending=fn;return 123;}
static int file_open(const char *p,int flags,...)
{char path[1100];assert(!strncmp(p,"/data/",6)||!strncmp(p,"/ufs/",5));
 snprintf(path,sizeof(path),"%s%s",root,p);return open(path,flags,0600);}
static int file_statfs(const char *p,struct statfs *s)
{assert(!strcmp(p,"/ufs"));memset(s,0,sizeof(*s));s->f_type=0x4d44;return 0;}
static ssize_t file_read(int f,void *p,size_t n)
{if(fault==1&&++calls==1){errno=EINTR;return -1;}
 if(fault==2)return 0;
 return read(f,p,n>333?333:n);}
static ssize_t file_write(int f,const void *p,size_t n);
static int file_sync(int f)
{if(fault==5){errno=EIO;return -1;}return fsync(f);}
static int file_close(int f)
{int r=close(f);if(fault==6){errno=EIO;return -1;}return r;}
#define MSDOS_SUPER_MAGIC 0x4d44
#define task_create fake_task
#define open file_open
#define read file_read
#define write file_write
#define statfs(...) file_statfs(__VA_ARGS__)
#define fsync file_sync
#define close file_close
#define main storage_main
#include "../app/a733storage/a733storage_main.c"
#undef main
#undef open
#undef read
#undef write
#undef statfs
#undef fsync
#undef close
static ssize_t file_write(int f,const void *p,size_t n)
{if(fault==3)return 0;
 if(fault==4)atomic_store(&g_stop,true);
 if(fault==7){uint8_t *bad=malloc(n);assert(bad);memcpy(bad,p,n);bad[0]^=1;
   ssize_t r=write(f,bad,n);free(bad);return r;}
 return write(f,p,n>257?257:n);}
static int command(const char *op,const char *src,const char *dest)
{char *a[]={"ufs",(char *)op,(char *)src,(char *)dest,NULL};
 return storage_main(dest?4:src?3:2,a);}
static void run_pending(void)
{assert(pending);int (*fn)(int,char **)=pending;pending=NULL;fn(0,NULL);assert(!g_job.active);}
int main(int argc,char **argv)
{
 assert(argc==2&&strlen(argv[1])<sizeof(root));strcpy(root,argv[1]);
 char name[1200];snprintf(name,sizeof(name),"%s/data/input",root);
 FILE *s=fopen(name,"wb");assert(s);for(unsigned i=0;i<700123;i++)fputc((i^(i>>9))&255,s);assert(!fclose(s));
 assert(!path_ok("/dev/ufs0")&&!path_ok("/ufs/a/../b")&&!path_ok("/ufs/./b"));
 assert(command("copy","/data/input","/data/nope"));
 assert(!command("copy","/data/input","/ufs/good"));
 assert(command("readbench","/data/input",NULL)); /* one job only */
 run_pending();assert(g_job.verified&&!g_job.result&&g_job.done==700123&&g_job.checked==700123);
 assert(!command("copy","/data/input","/ufs/good"));run_pending();assert(g_job.result==-EEXIST&&!g_job.created);
 assert(!command("compare","/data/input","/ufs/good"));run_pending();assert(g_job.verified);
 assert(!command("readbench","/ufs/good","1"));run_pending();assert(!g_job.result&&!g_job.verified);
 for(int mode=1;mode<=7;mode++)
   {
     fault=mode;calls=0;char dest[64];snprintf(dest,sizeof(dest),"/ufs/fault-%d",mode);
     assert(!command("copy","/data/input",dest));run_pending();
     if(mode==1)assert(!g_job.result&&g_job.verified);
     else assert(g_job.result<0&&!g_job.verified&&g_job.created);
     if(mode==3)assert(g_job.result==-EIO);
     if(mode==4)assert(g_job.result==-ECANCELED);
     if(mode==7)assert(g_job.result==-EBADMSG);
   }
 fault=0;assert(!command("copy","/data/input","/ufs/cancel"));
 assert(!command("stop",NULL,NULL));run_pending();assert(g_job.result==-ECANCELED);
 assert(!command("compare","/data/input","/ufs/fault-2"));run_pending();assert(g_job.result==-EBADMSG);
 assert(!command("compare","/data/input","/ufs/fault-7"));run_pending();assert(g_job.result==-EBADMSG);
 assert(command("readbench","/data/input","0"));
 assert(command("readbench","/data/input","257"));
 assert(!command("copy","/data/input","/ufs/deadline"));
 g_job.start_ms=now_ms()-JOB_LIMIT_MS-1;
 run_pending();assert(g_job.result==-ETIMEDOUT&&!g_job.verified);
 snprintf(name,sizeof(name),"%s/data/empty",root);s=fopen(name,"wb");assert(s&&!fclose(s));
 assert(!command("copy","/data/empty","/ufs/empty"));run_pending();assert(!g_job.result&&g_job.verified);
 snprintf(name,sizeof(name),"%s/data/huge",root);int sparse=open(name,O_WRONLY|O_CREAT|O_EXCL,0600);
 assert(sparse>=0&&!ftruncate(sparse,UINT64_C(4294967296))&&!close(sparse));
 assert(!command("copy","/data/huge","/ufs/huge"));run_pending();
 assert(g_job.result==-EFBIG&&!g_job.created&&!g_job.verified);
 assert(!command("status",NULL,NULL));
 puts("Storage application PASS: actual code/SHA256, short I/O, EINTR, zero progress, stop, sync/close errors, corruption, existing-file preservation; no HW");
 return 0;
}
