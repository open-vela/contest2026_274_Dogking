#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Test actual UART4 open/close/read/write/ioctl bodies against host MMIO seams."""
import re
import subprocess
import tempfile
from pathlib import Path

repo = Path(__file__).resolve().parents[1]
source = (repo / 'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733/a733_uart4.c').read_text()


def function(name):
    match = re.search(r'^static [^;{]*\b' + name + r'\([^;]*?\n\{', source, re.M)
    return source[match.start():source.index('\n}', match.end()) + 2]


assert '#define A733_UART4_BAUD        115200u' in source
assert '.close = a733_uart4_close' in source
assert '8u * A733_UART4_BAUD' in source
fixture = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <time.h>
#define OK 0
#define A733_UART4_BASE 0x02504000u
#define A733_UART4_BAUD 115200u
#define A733_UART4_TIMEOUT 1000
#define UART_LSR 0x14
#define UART_USR 0x7c
#define UART_RBR 0
#define UART_THR 0
#define UART_LSR_DR 1
#define UART_LSR_THRE 32
#define UART_USR_TFNF 2
#define TCGETS 1
#define TCSETS 2
#define TCSETSW 3
#define TCSETSF 4
#define B115200 5
#define CS8 0x30
#define CSIZE 0x30
#define PARENB 0x40
#define CSTOPB 0x80
#define CREAD 0x100
#define CLOCAL 0x200
struct termios {unsigned c_cflag,c_speed;};
struct file {int f_oflags;};
static int g_uart4_lock;
static bool g_uart4_opened;
static unsigned ticks,tx,rx;
static bool ready=true;
static int nxmutex_lock(int *p){(void)p;return 0;}
static int nxmutex_unlock(int *p){(void)p;return 0;}
static unsigned clock_systime_ticks(void){return ticks++;}
static void nxsig_usleep(unsigned n){(void)n;}
static unsigned cfgetospeed(struct termios *t){return t->c_speed;}
static unsigned getreg32(uintptr_t p){
  if(p==A733_UART4_BASE+UART_LSR)return (ready?UART_LSR_THRE:0)|(rx?UART_LSR_DR:0);
  if(p==A733_UART4_BASE+UART_USR)return ready?UART_USR_TFNF:0;
  assert(p==A733_UART4_BASE);--rx;return 0xa5;
}
static void putreg32(unsigned v,uintptr_t p){assert(v==0xa5&&p==A733_UART4_BASE);tx++;}
'''
fixture += '\n'.join(function(n) for n in ('a733_uart4_open', 'a733_uart4_close',
    'a733_uart4_read', 'a733_uart4_write', 'a733_uart4_ioctl'))
fixture += r'''
int main(void){
  struct file f={O_NONBLOCK};struct termios t;char b=0xa5;
  assert(a733_uart4_open(&f)==0&&a733_uart4_open(&f)==-EBUSY);
  assert(a733_uart4_ioctl(&f,TCGETS,(unsigned long)&t)==0&&t.c_speed==115200);
  assert(a733_uart4_ioctl(&f,TCSETS,(unsigned long)&t)==0);
  t.c_speed=9600;assert(a733_uart4_ioctl(&f,TCSETS,(unsigned long)&t)==-EINVAL);
  t.c_speed=115200;t.c_cflag|=PARENB;assert(a733_uart4_ioctl(&f,TCSETS,(unsigned long)&t)==-EINVAL);
  assert(a733_uart4_ioctl(&f,TCSETS,0)==-EINVAL);
  assert(a733_uart4_read(&f,&b,1)==-EAGAIN);
  rx=1;assert(a733_uart4_read(&f,&b,1)==1&&(unsigned char)b==0xa5);
  assert(a733_uart4_write(&f,&b,1)==1&&tx==1);
  ready=false;assert(a733_uart4_write(&f,&b,1)==-EAGAIN&&tx==1);
  f.f_oflags=0;assert(a733_uart4_write(&f,&b,1)==-ETIMEDOUT&&tx==1);
  assert(a733_uart4_close(&f)==0&&a733_uart4_open(&f)==0);
  assert(a733_uart4_close(&f)==0);
  puts("PASS: UART4 actual driver bodies: exclusive owner, 115200 8N1, reject 9600, RX, bounded/nonblocking TX");
}
'''
with tempfile.TemporaryDirectory(prefix='uart4-mcu-') as tmp:
    root = Path(tmp)
    (root / 'test.c').write_text(fixture)
    subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    '-fsanitize=address,undefined', str(root / 'test.c'), '-o', str(root / 'test')], check=True)
    subprocess.run([str(root / 'test')], check=True, timeout=10)
