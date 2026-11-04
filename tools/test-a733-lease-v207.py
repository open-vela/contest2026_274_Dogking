#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile actual ioctl/close/write ownership boundaries, not a string mock."""
from pathlib import Path
import re,subprocess,tempfile
r=Path(__file__).resolve().parents[1];overlay=r/'board/a733-cubie-a7z/openvela-overlay'
runtime=(overlay/'vendor/allwinnertech/chips/a733/a733_npu_runtime.c').read_text()
functions=[]
for declaration in ('static int a733_rt_ioctl(', 'static int a733_rt_close(', 'static ssize_t a733_rt_write('):
    start=runtime.index(declaration);end=runtime.index('\n}',start)+2;functions.append(runtime[start:end])
assert 'if(!a733_rt_live_verify_load(i,&regions[i],&mapped[i]))outputs_valid=false;' in runtime
assert runtime.index('if(!a733_rt_live_verify_load(')<runtime.index('else if (!outputs_valid)')
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <nuttx/npu/a733_vip2.h>
#define CONFIG_A733_CAMERA_PERF_TEST 1
struct file{int unused;};
static int g_rt_tensor_gate,locked,backend_calls;
static bool lock_fail,g_rt_tensor_quarantined;
static void *g_rt_live_owner;
static int nxmutex_lock(int*p){(void)p;assert(!locked);if(lock_fail)return -EBUSY;locked=1;return 0;}
static void nxmutex_unlock(int*p){(void)p;assert(locked);locked=0;}
static int a733_rt_ioctl_impl(struct file*f,int cmd,unsigned long arg)
{(void)f;(void)arg;assert(cmd==A733_NPUIOC_CANCEL || locked);++backend_calls;return 0;}
static int a733_rt_close_impl(struct file*f){(void)f;assert(locked);++backend_calls;return 0;}
static ssize_t a733_rt_write_impl(struct file*f,const char*b,size_t n)
{(void)f;(void)b;assert(locked);++backend_calls;return (ssize_t)n;}
'''+ '\n'.join(functions)+r'''
int main(void)
{
  struct file f={0};
  for(int n=0;n<256;++n)
    {
      int cmd=_DIOC(n);g_rt_live_owner=&f;
      assert(!a733_rt_ioctl(&f,cmd,1) && !locked);
      bool read_or_run=cmd==A733_NPUIOC_CANCEL || cmd==A733_NPUIOC_LIVE_RUN ||
        cmd==A733_NPUIOC_LIVE_PROFILE || cmd==A733_NPUIOC_GET_INFO;
      assert((g_rt_live_owner!=NULL)==read_or_run);
    }
  g_rt_live_owner=&f;assert(!a733_rt_close(&f) && !g_rt_live_owner && !locked);
  g_rt_live_owner=&f;assert(a733_rt_write(&f,"x",1)==1 && !g_rt_live_owner);
  g_rt_live_owner=&f;lock_fail=true;int before=backend_calls;
  assert(a733_rt_ioctl(&f,A733_NPUIOC_RESET,1)==-EBUSY && g_rt_live_owner==&f && backend_calls==before);
  lock_fail=false;g_rt_tensor_quarantined=true;
  assert(a733_rt_ioctl(&f,A733_NPUIOC_RESET,1)==-EIO && !g_rt_live_owner && backend_calls==before);
  assert(a733_rt_write(&f,"x",1)==-EIO && !locked);
  puts("PASS actual 256 ioctl lease boundaries, foreign close/write/SYNC invalidation, lock failure and quarantine; HOST ONLY");
}
'''
with tempfile.TemporaryDirectory(prefix='lease207-') as raw:
    tmp=Path(raw);src=tmp/'test.c';exe=tmp/'test';src.write_text(code)
    subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-fno-pie','-no-pie','-fsanitize=address,undefined',
      '-I'+str(r/'tools/live-host-fixture'),'-I'+str(overlay/'nuttx/include'),str(src),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=30)
