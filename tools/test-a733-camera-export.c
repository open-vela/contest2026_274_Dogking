/* SPDX-License-Identifier: Apache-2.0 */
/* HOST ONLY: actual file/export CLI, simulated fixed device nodes. */
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#define FRAME_FD 40001
#define CONTROL_FD 40002
#define REPORT_FD 40003
static size_t frame_pos,report_pos;
static bool frame_ready=true;
static int marker_fd=-1;
static int fake_open(const char *path,int flags,...)
{
  if(!strcmp(path,"/dev/a733-camera-frame"))
    {if(!frame_ready || getenv("FRAME_NOT_READY")){errno=ENODATA;return -1;}frame_pos=0;return FRAME_FD;}
  if(!strcmp(path,"/dev/a733-camera-control"))
    {report_pos=0;return flags&O_WRONLY?CONTROL_FD:REPORT_FD;}
  if(flags&O_CREAT)
    {va_list args;va_start(args,flags);int mode=va_arg(args,int);va_end(args);
     int fd=open(path,flags,mode);if(strstr(path,"/COMPLETE.partial"))marker_fd=fd;return fd;}
  return open(path,flags);
}
static ssize_t fake_read(int fd,void *buffer,size_t n)
{
  if(fd==FRAME_FD)
    {
      size_t size=3840u*1080u;if(getenv("FRAME_SHORT"))size-=2;
      if(frame_pos>=size)return 0;
      if(n>size-frame_pos)n=size-frame_pos;
      if(n>12345)n=12345;
      uint8_t *out=buffer;
      for(size_t i=0;i<n;++i)
        {size_t p=frame_pos+i;unsigned value=(p/2)%1024;out[i]=p%2?value>>8:value&255;}
      frame_pos+=n;return n;
    }
  if(fd==REPORT_FD)
    {
      const char text[]="HOST SIMULATED FRAME DEVICE; NOT HARDWARE ACCEPTANCE\n";
      size_t len=sizeof(text)-1;if(n>len-report_pos)n=len-report_pos;
      memcpy(buffer,text+report_pos,n);report_pos+=n;return n;
    }
  return read(fd,buffer,n);
}
static ssize_t fake_write(int fd,const void *p,size_t n)
{
  if(fd!=CONTROL_FD)
    {if(fd==marker_fd && getenv("FAIL_MARKER_WRITE")){errno=ENOSPC;return -1;}return write(fd,p,n);}
  if(n==7 && !memcmp(p,"prepare",7))
    {if(getenv("FRAME_NO_HANDOFF")){errno=ENODATA;return -1;}frame_ready=false;return n;}
  if(n==16 && !memcmp(p,"frame-experiment",16))
    {if(getenv("FRAME_CAPTURE_ERROR")){errno=EIO;return -1;}frame_ready=true;return n;}
  errno=EINVAL;return -1;
}
static int fake_close(int fd)
{return fd==FRAME_FD || fd==CONTROL_FD || fd==REPORT_FD?0:close(fd);}
static int fake_fstat(int fd,struct stat *st)
{if(fd!=FRAME_FD)return fstat(fd,st);memset(st,0,sizeof(*st));st->st_mode=S_IFCHR|0400;return 0;}
#define open fake_open
#define read fake_read
#define write fake_write
#define close fake_close
#define fstat fake_fstat
#define main camera_cli_entry
#include "../board/a733-cubie-a7z/openvela-overlay/apps/system/a733camera/a733camera_main.c"
#undef main
int main(int argc,char **argv){return camera_cli_entry(argc,argv);}
