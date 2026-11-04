#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Execute actual capture loop with synthetic FIFO; not microphone proof."""
import re
import subprocess
import tempfile
from pathlib import Path

repo = Path(__file__).resolve().parents[1]
base = repo / 'board/a733-cubie-a7z/openvela-overlay'
source = (base / 'vendor/allwinnertech/chips/a733/a733_i2s0_audio.c').read_text()

def fn(name):
    start = source.index('static ', source.index(name) - 30)
    op = source.index('\n{', start)
    return source[start:source.index('\n}', op) + 2]

defs = '\n'.join(re.findall(r'^#define (?:I2S_|A733_)[^\n]*(?:\\\n[^\n]*)*', source, re.M))
# Greedy first-line matching can include the continuation backslash; include
# the known PIO continuation explicitly instead of relying on compiler errors.
defs = defs.replace('\\\n', '')
defs = re.sub(r'#define A733_PB_BASE.*?(?=\n#define)',
              '#define A733_PB_BASE UINT64_C(0x02000100)', defs, flags=re.S)
diag = re.search(r'struct a733_i2s0_diag_s\n\{.*?\n\};', source, re.S)[0]
prefix = r'''
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <sys/a733_audio.h>
typedef int mutex_t;
struct file {int dummy;};
#define TICK2MSEC(v) (v)
#define kmm_malloc malloc
#define kmm_free free
#define get_errno() errno
'''
stubs = r'''
static struct a733_i2s0_diag_s g_i2s0;
static unsigned words,callbacks,stop_after,mode,stops,quiet;
static size_t strlcpy(char *d,const char *s,size_t cap){size_t n=strlen(s);if(cap){size_t c=n<cap-1?n:cap-1;memcpy(d,s,c);d[c]=0;}return n;}
static int a733_i2s0_hw_initialize(void){return 0;}
static void a733_i2s0_stream_stop(void){++stops;}
static void a733_i2s0_probe(void){}
static void a733_i2s0_set_clock_polarity(unsigned n){assert(!n);}
static void a733_i2s0_set_sample_resolution(unsigned n){assert(n==I2S_FMT0_RES_24);}
static void a733_pb_pull(unsigned pin,unsigned pull){assert(pin==8&&pull==2);}
static void a733_i2s0_configure_rx(bool ics,unsigned slot){assert(ics&&!slot);}
static void a733_i2s0_dout_quiet(void){++quiet;}
static int a733_i2s0_wait_rx_data(uint32_t *fifo){*fifo=2;return 0;}
static clock_t clock_systime_ticks(void){return words/96;}
static uint32_t getreg32(uintptr_t addr){
 if(addr==A733_I2S0_BASE+I2S_RXFIFO){unsigned slot=words++%2;return slot?0:(1000u<<8);}
 if(addr==A733_I2S0_BASE+I2S_RXCNT)return words/2;
 if(addr==A733_I2S0_BASE+I2S_INTSTA)return mode==3?I2S_INTSTA_RX_OVERRUN:0;
 return 0;
}
static void putreg32(uint32_t v,uintptr_t addr){
 if(addr==A733_I2S0_BASE+I2S_CTL)assert(!(v&I2S_CTL_TXEN)&&!(v&I2S_CTL_SDO0_EN));
}
static int a733_i2s0_record_rx_counter(uint32_t count,uint32_t channels){assert(channels==2&&count==words);return mode==4?-EIO:0;}
static int file_open(struct file *f,const char *p,int flags,int perm){(void)f;(void)p;(void)flags;(void)perm;assert(false);return -EIO;}
static int file_close(struct file *f){(void)f;assert(false);return -EIO;}
static int nx_unlink(const char *p){(void)p;assert(false);return -EIO;}
static int a733_file_write_all(struct file *f,const void *p,size_t n){(void)f;(void)p;(void)n;assert(false);return -EIO;}
static void a733_put_le16(uint8_t *p,unsigned n){(void)p;(void)n;assert(false);}
static void a733_put_le32(uint8_t *p,uint32_t n){(void)p;(void)n;assert(false);}
static int receive(void *cookie,const uint8_t *data,uint32_t bytes){
 assert(cookie==&mode&&bytes==960);const int16_t *pcm=(const int16_t *)data;
 for(unsigned i=0;i<480;i++)assert(pcm[i]==5000);
 ++callbacks;
 if(mode==1)return -ECANCELED;
 if(mode==2)return 2;
 return stop_after&&callbacks==stop_after?1:0;
}
static void reset(unsigned fault,unsigned end){words=callbacks=stops=quiet=0;mode=fault;stop_after=end;memset(&g_i2s0,0,sizeof(g_i2s0));}
'''
tests = r'''
int main(void){
 for(unsigned end=0;end<=3;end++){
  reset(0,end);struct a733_audio_live_s live={A733_AUDIO_ABI,200,5,receive,&mode,0,0};
  assert(!a733_i2s0_capture_into(200,false,0,0,2,false,true,true,5,NULL,&live));
  unsigned expected=end?end*480:9600;
  assert(live.frames==expected&&live.ended==!!end&&words==9600+expected*2);
  assert(g_i2s0.capture_target_rx==words&&g_i2s0.capture_consumed_rx==words);
  assert(callbacks==(end?end:20)&&quiet==1&&stops==1);
 }
 for(unsigned fault=1;fault<=4;fault++){
  reset(fault,0);struct a733_audio_live_s live={A733_AUDIO_ABI,200,5,receive,&mode,0,0};
  assert(a733_i2s0_capture_into(200,false,0,0,2,false,true,true,5,NULL,&live)<0);
  assert(!live.frames&&!live.ended&&stops==1);
 }
 puts("PASS: actual RX loop: live x5 mono, early endpoint at complete stereo frame, no files/TX, callback/FIFO/counter errors with no qualified publication (synthetic FIFO)");
}
'''
with tempfile.TemporaryDirectory(prefix='a733-live-test-') as tmp:
    src = Path(tmp) / 'test.c'
    src.write_text(prefix + defs + '\n' + diag + stubs + '\n' +
                   fn('a733_i2s0_capture_stats(') + '\n' +
                   fn('a733_i2s0_apply_gain(') + '\n' +
                   fn('a733_i2s0_capture_into(') + '\n' + tests)
    binary = Path(tmp) / 'test'
    subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I' + str(base / 'nuttx/include'),
                    str(src), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
