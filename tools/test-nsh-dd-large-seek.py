#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Apply real project patch to a private SDK-source copy; exercise actual NSH
dd bodies on sparse regular files. Never open a block device or edit the SDK.
Linux host only. Include lines alone are replaced with POSIX/NSH test shims.
"""
from pathlib import Path
import argparse,hashlib,os,subprocess,tempfile
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('sdk',type=Path);a=p.parse_args()
repo=Path(__file__).resolve().parent.parent
original=(a.sdk/'apps/nshlib/nsh_ddcmd.c').read_bytes()
prefix=r'''
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#define FAR
#define OK 0
#define ERROR (-1)
#define UNUSED(x) ((void)(x))
#define NSH_ERRNO errno
#define NSH_ERRNO_OF(x) (x)
struct nsh_vtbl_s {int unused;};
struct console_stdio_s {struct nsh_vtbl_s vtbl;int in,out;};
#define INFD(p) ((p)->in)
#define OUTFD(p) ((p)->out)
static const char *g_fmtcmdfailed="failed",*g_fmtargrequired="required",
                  *g_fmtcmdoutofmemory="nomem";
static unsigned errors,ios,seek_calls,fail_seek;
#define nsh_error(...) (++errors)
#define nsh_output(...) ((void)0)
#define nsh_dumpbuffer(...) ((void)0)
static char *nsh_getfullpath(struct nsh_vtbl_s *v,const char *p)
{(void)v;return strdup(p);}
#define nsh_freefullpath free
/* NuttX O_RDONLY is a BIT, unlike Linux's zero. Translate access mode in
 * the shim so the actual production verify branch is exercised too. */
static int host_open(const char *p,int flags,...)
{
  int access=(flags&0x10000000)?((flags&O_WRONLY)?O_RDWR:O_RDONLY):O_WRONLY;
  return open(p,(flags&~(0x10000000|O_ACCMODE))|access,0600);
}
static off_t host_seek(int fd,off_t n,int whence)
{++seek_calls;if(seek_calls==fail_seek){errno=ESPIPE;return -1;}return lseek(fd,n,whence);}
static ssize_t host_read(int fd,void *p,size_t n){++ios;return read(fd,p,n);}
static ssize_t host_write(int fd,const void *p,size_t n){++ios;return write(fd,p,n);}
#undef O_RDONLY
#define O_RDONLY 0x10000000
#define open host_open
#define lseek host_seek
#define read host_read
#define write host_write
'''
suffix=r'''
#undef open
#undef lseek
#undef read
#undef write
int main(int argc,char **argv)
{
  assert(argc==3);bool fixed=atoi(argv[2]);char in[512],out[512],ai[600],ao[600];
  snprintf(in,sizeof(in),"%s/input",argv[1]);snprintf(out,sizeof(out),"%s/output",argv[1]);
  snprintf(ai,sizeof(ai),"if=%s",in);snprintf(ao,sizeof(ao),"of=%s",out);
  int fd=open(in,O_RDWR|O_CREAT|O_TRUNC,0600);assert(fd>=0);
  const off_t offsets[]={2147479552LL,2147483648LL,2684354560LL,
                        4294967296LL,6442450944LL,128026607616LL};
  uint8_t block[4096],check[4096];
  for(unsigned i=0;i<sizeof(block);i++)block[i]=(uint8_t)(i^(i>>7)^0xa7);
  for(unsigned i=0;i<6;i++)assert(pwrite(fd,block,sizeof(block),offsets[i])==4096);
  assert(close(fd)==0);struct console_stdio_s con={0};con.in=0;con.out=1;
  for(unsigned i=0;i<6;i++)
    {
      char skip[64];snprintf(skip,sizeof(skip),"skip=%" PRId64,(int64_t)(offsets[i]/4096));
      char *args[]={"dd",ai,ao,"bs=4096","count=1",skip};errno=ENOENT;
      errors=ios=seek_calls=fail_seek=0;
      int ret=cmd_dd(&con.vtbl,6,args);
      bool narrowed=(int32_t)offsets[i]<0;
      if(!fixed&&narrowed){assert(ret<0&&errors&&ios==0);continue;}
      assert(!ret&&!errors&&ios==2);fd=open(out,O_RDWR);assert(fd>=0);
      assert(pread(fd,check,4096,0)==4096&&!memcmp(block,check,4096));close(fd);
      /* Positive high offsets on output and verification input too. */
      char seek[64];snprintf(seek,sizeof(seek),"seek=%" PRId64,(int64_t)(offsets[i]/4096));
      char *put[]={"dd",ao,ai,"bs=4096","count=1",seek,"conv=notrunc"};
      errors=ios=seek_calls=0;assert(!cmd_dd(&con.vtbl,7,put)&&!errors);
      char *verify[]={"dd",ai,ao,"bs=4096","count=1",skip,"verify"};
      errors=ios=seek_calls=0;assert(!cmd_dd(&con.vtbl,7,verify)&&!errors&&seek_calls==3);
    }
  if(fixed)
    {
      for(unsigned failure=1;failure<=3;failure++)
        {
          char *args[]={"dd",ai,ao,"bs=4096","count=1","skip=31256496","verify"};
          errors=ios=seek_calls=0;fail_seek=failure;
          assert(cmd_dd(&con.vtbl,7,args)<0&&errors&&seek_calls==failure);
          if(failure==1)assert(!ios);
        }
      fail_seek=1;errors=ios=seek_calls=0;
      char *put[]={"dd",ao,ai,"bs=4096","count=1","seek=31256496","conv=notrunc"};
      assert(cmd_dd(&con.vtbl,7,put)<0&&errors&&!ios);
    }
  unlink(in);unlink(out);
  printf("actual NSH dd %s: 2GiB/4GiB/128GB skip+seek+verify and true seek faults PASS\n",fixed?"patched":"baseline-reproduction");
  return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='nsh-dd-') as folder:
    tmp=Path(folder);src=tmp/'apps/nshlib/nsh_ddcmd.c';src.parent.mkdir(parents=True)
    src.write_bytes(original)
    subprocess.run(['patch','--batch','--fuzz=0','-p1','-d',str(tmp),'-i',
                    str(repo/'patches/nsh-dd-large-seek.patch')],check=True)
    patched=src.read_bytes()
    assert patched!=original and b'ret = lseek' not in patched
    for name,body,fixed in [('baseline',original,0),('patched',patched,1)]:
        c=tmp/(name+'.c');exe=tmp/name
        c.write_text(prefix+'\n'.join(line for line in body.decode().splitlines()
            if not line.startswith('#include'))+suffix)
        subprocess.run(['gcc','-std=c11','-O1','-g','-Wno-unused-variable',
            '-fno-pie','-no-pie','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
        subprocess.run([str(exe),str(tmp),str(fixed)],check=True,timeout=30,
                       env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1'})
    print('primary SHA256='+hashlib.sha256(original).hexdigest())
    print('patched SHA256='+hashlib.sha256(patched).hexdigest())
