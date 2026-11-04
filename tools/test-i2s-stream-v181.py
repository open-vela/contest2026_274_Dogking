#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Production streaming TX function with simulated MMIO, NOT board timing."""
import re
import subprocess
import tempfile
from pathlib import Path

repo = Path(__file__).resolve().parents[1]
base = repo / 'board/a733-cubie-a7z/openvela-overlay'
driver = (base / 'vendor/allwinnertech/chips/a733/a733_i2s0_audio.c').read_text()
play = (base / 'vendor/allwinnertech/chips/a733/a733_i2s0_playback.inc').read_text()

def function(source, name):
    start = source.index('static ', source.index(name) - 25)
    opening = source.index('\n{', start)
    return source[start:source.index('\n}', opening) + 2]

defines = '\n'.join(line for line in (driver + '\n' + play).splitlines()
                    if re.match(r'#define (I2S_|A733_I2S0_BASE |A733_TX_ERRORS )', line))
structure = re.search(r'struct a733_i2s0_diag_s\n\{.*?\n\};', driver, re.S)[0]
prefix = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <sys/a733_audio.h>
typedef int mutex_t;
#define TICK2MSEC(x) (x)
'''
stubs = r'''
static struct a733_i2s0_diag_s g_i2s0;
static uint32_t words, enables, initialized, stops, error_flags;
static unsigned source_calls, total_samples, source_position, bad;
static clock_t extra_ticks;
static size_t strlcpy(char *d,const char *s,size_t cap){size_t n=strlen(s);if(cap){size_t c=n<cap-1?n:cap-1;memcpy(d,s,c);d[c]=0;}return n;}
static int a733_i2s0_hw_initialize(void){++initialized;return 0;}
static void a733_i2s0_stream_stop(void){++stops;}
static void a733_i2s0_set_clock_polarity(unsigned n){assert(!n);}
static void a733_i2s0_set_sample_resolution(unsigned n){assert(n==I2S_FMT0_RES_16);}
static int a733_i2s0_wait_tx_space(void){return 0;}
static void a733_pb_pinmux(unsigned pin,unsigned function){assert(pin==7&&function==3);}
static clock_t clock_systime_ticks(void){return words/96+extra_ticks;}
static void up_udelay(unsigned n){assert(n==50);}
static uint32_t getreg32(uintptr_t addr){
 if(addr==A733_I2S0_BASE+I2S_INTSTA)return error_flags;
 if(addr==A733_I2S0_BASE+I2S_TXCNT)return words;
 assert(addr==A733_I2S0_BASE+I2S_FIFOSTA);return 128u<<16;
}
static void putreg32(uint32_t v,uintptr_t addr){
 if(addr==A733_I2S0_BASE+I2S_CTL){assert(v);++enables;return;}
 assert(addr==A733_I2S0_BASE+I2S_TXCNT||addr==A733_I2S0_BASE+I2S_INTSTA);
}
static void putreg16(uint16_t v,uintptr_t addr){
 assert(addr==A733_I2S0_BASE+I2S_TXFIFO);assert(v==500);++words;
}
static int a733_i2s0_tx_prepare(bool fifo16){
 assert(fifo16);a733_i2s0_stream_stop();a733_i2s0_set_clock_polarity(0);
 a733_i2s0_set_sample_resolution(I2S_FMT0_RES_16);return 0;
}
static void a733_i2s0_tx_sample(int16_t v){assert(v==500);++g_i2s0.tx_samples;}
static int a733_i2s0_tx_begin(void){
 a733_pb_pinmux(7,3);putreg32(I2S_CTL_GLOBAL_EN,A733_I2S0_BASE+I2S_CTL);return 0;
}
static int read_pcm(void *cookie,uint8_t *out,uint32_t cap,uint32_t *bytes){
 assert(cookie==&bad&&cap==960);++source_calls;*bytes=0;
 if(bad==1)return -EPIPE;
 if(bad==2){*bytes=961;return 0;}
 if(bad==3){*bytes=3;return 0;}
 if(bad==4&&source_calls>1)return -EIO;
 if(bad==5&&source_calls>1)error_flags=1u<<5;
 if(bad==6&&source_calls>1)extra_ticks=11000;
 unsigned n=total_samples-source_position;if(n>480)n=480;
 for(unsigned i=0;i<n;i++){out[2*i]=0xe8;out[2*i+1]=3;}
 source_position+=n;*bytes=2*n;return 0;
}
static void reset(unsigned samples,unsigned fault){
 words=enables=initialized=stops=error_flags=source_calls=source_position=0;
 extra_ticks=0;total_samples=samples;bad=fault;memset(&g_i2s0,0,sizeof(g_i2s0));
}
'''
tests = r'''
int main(void){
 for(unsigned n=1;n<=4800;n=n==1?63:n==63?64:n==64?480:n==480?4800:4801){
  reset(n,0);struct a733_audio_stream_s in={A733_AUDIO_ABI,50,read_pcm,&bad,99};
  assert(!a733_i2s0_play_stream(&in));assert(in.frames==n&&words==2*n);
  assert(enables==1&&initialized==1&&stops==2&&g_i2s0.play_status==0);
 }
 for(unsigned fault=1;fault<=6;fault++){
  reset(2000,fault);struct a733_audio_stream_s in={A733_AUDIO_ABI,50,read_pcm,&bad,99};
  assert(a733_i2s0_play_stream(&in)<0);
  if(fault<=3)assert(!initialized&&!enables&&!in.frames);
  else assert(stops==2&&in.frames>0&&in.frames<2000);
 }
 reset(0,0);struct a733_audio_stream_s in={A733_AUDIO_ABI,50,read_pcm,&bad,99};
 assert(a733_i2s0_play_stream(&in)==-ENODATA&&!initialized);
 puts("PASS: actual I2S stream TX function: 1/63/64/480/4800 frames, stereo duplication, volume, bounded faults, prebuffer safety and cleanup (simulated MMIO)");
}
'''
with tempfile.TemporaryDirectory(prefix='a733-stream-test-') as tmp:
    src = Path(tmp) / 'test.c'
    src.write_text(prefix + defines + '\n' + structure + stubs +
                   function(play, 'a733_play_le16(') + '\n' +
                   function(play, 'a733_i2s0_play_stream(') + '\n' + tests)
    exe = Path(tmp) / 'test'
    subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I' + str(base / 'nuttx/include'),
                    str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
