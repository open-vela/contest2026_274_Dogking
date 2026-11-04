#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Actual monitor loop/counter checker, synthetic MMIO/time; NOT board proof."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1];base=r/'board/a733-cubie-a7z/openvela-overlay'
s=(base/'vendor/allwinnertech/chips/a733/a733_i2s0_audio.c').read_text()
def function(name):
    start=s.index('static int '+name+'(');op=s.index('\n{',start)
    return s[start:s.index('\n}',op)+2]
macros=s[s.index('#define A733_CCU_BASE'):s.index('\nstruct a733_i2s0_diag_s')]
source=r'''
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/a733_audio.h>
'''+macros+r'''
static struct {char stage[48],last_operation[48];uint32_t rx_count,capture_intsta,capture_fifo_peak;
 int last_capture_status;bool last_capture_committed;} g_i2s0;
static unsigned mode,calls,stops;static uint64_t words;
static clock_t clock_systime_ticks(void){return words*1000/(mode==9?9600:mode==10?48000:96000);}
#define TICK2MSEC(t) (t)
static int a733_i2s0_hw_initialize(void){return mode==1?-ENODEV:0;}
static void a733_i2s0_stream_stop(void){++stops;}
static void a733_i2s0_set_sample_resolution(unsigned v){assert(v==I2S_FMT0_RES_24);}
static void a733_i2s0_set_clock_polarity(unsigned v){assert(!v);}
static void a733_pb_pull(unsigned p,unsigned v){assert(p==8&&v==2);}
static void a733_i2s0_configure_rx(bool stereo,unsigned slot){assert(stereo&&!slot);}
static void a733_i2s0_dout_quiet(void){}
static size_t copy_s(char *out,const char *in,size_t n){assert(strlen(in)<n);strcpy(out,in);return strlen(in);}
#define strlcpy copy_s
static void putreg32(uint32_t v,uintptr_t address){
 if(address==A733_I2S0_BASE+I2S_CTL)assert((v&I2S_CTL_RXEN)&&!(v&I2S_CTL_TXEN));
}
static uint32_t getreg32(uintptr_t address){
 if(address==A733_I2S0_BASE+I2S_RXFIFO){++words;return words%2?0x10000:0;}
 if(address==A733_I2S0_BASE+I2S_RXCNT)return (uint32_t)(words/2)+(mode==6?-1:mode==7?65:0);
 assert(address==A733_I2S0_BASE+I2S_INTSTA);return mode==8?I2S_INTSTA_RX_OVERRUN:0x11;
}
static int a733_i2s0_wait_rx_data(uint32_t *out){*out=2;return mode==5&&words>=10000?-ETIMEDOUT:0;}
static int consume(void *p,const uint8_t *pcm,uint32_t n){
 assert(p==&calls&&n==960);++calls;
 for(unsigned i=0;i<480;i++)assert((pcm[2*i]|(unsigned)pcm[2*i+1]<<8)==1280);
 if(calls==4&&mode==2)return -ECANCELED;
 if(calls==4&&mode==3)return 2;
 return calls==250?1:0;
}
'''+function('a733_i2s0_monitor_check')+'\n'+function('a733_i2s0_monitor')+r'''
int main(void){
 for(mode=0;mode<=10;mode++) {
  words=calls=stops=0;
  struct a733_audio_monitor_s request={.version=1,.gain=5,.consume=consume,.cookie=&calls};
  int rc=a733_i2s0_monitor(&request);
  if(mode==0||mode==4)assert(!rc&&request.ended&&request.frames==250*480&&stops==2);
  else {assert(rc<0&&!request.ended);if(mode!=1)assert(stops==2);}
  if(mode==1)assert(rc==-ENODEV);
  if(mode==2)assert(rc==-ECANCELED);
  if(mode==3)assert(rc==-EINVAL);
  if(mode==5||mode==9)assert(rc==-ETIMEDOUT);
  if(mode==6||mode==10)assert(rc==-EIO);
  if(mode==7||mode==8)assert(rc==-EOVERFLOW);
 }
 uint64_t n=2*(UINT64_C(0xffffffff)+50);
 assert(!a733_i2s0_monitor_check(n,49,0));
 assert(!a733_i2s0_monitor_check(n,50,0));
 assert(a733_i2s0_monitor_check(n,48,0)==-EIO);
 assert(a733_i2s0_monitor_check(n,114,0)==-EOVERFLOW);
 assert(a733_i2s0_monitor_check(n,49,I2S_INTSTA_RX_OVERRUN)==-EOVERFLOW);
 puts("PASS v214 actual monitor loop: complete stereo frame, left slot x5, RX-only/quiet output, callback protocol/cancel, FIFO fault, counter lag/lead/wrap, 250ms warmup bound, rolling rate proof, cleanup; MOCK MMIO");
}
'''
assert 'nxmutex_lock(&g_i2s0.lock)' not in s # direct cat/echo cannot hang behind idle monitor
with tempfile.TemporaryDirectory(prefix='monitor214-') as d:
    p=Path(d);(p/'test.c').write_text(source)
    subprocess.run(['gcc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror',
      '-fsanitize=address,undefined','-I'+str(base/'nuttx/include'),str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True,timeout=30)
