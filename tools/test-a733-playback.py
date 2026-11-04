#!/usr/bin/env python3
"""Production WAV parser/TX helpers with bounded fake FS and FIFO; not hardware proof."""
import os
import re
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
directory = root / 'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733'
driver = (directory / 'a733_i2s0_audio.c').read_text()
playback = (directory / 'a733_i2s0_playback.inc').read_text()


def function(name):
    match = re.search(r'^static [^;{]*\b' + name + r'\([^;]*?\n\{', driver, re.M)
    end = driver.index('\n}', match.end()) + 2
    return driver[match.start():end]


constants = '\n'.join(re.findall(r'^#define .*?(?<!\\)$', driver, re.M))
constants += '\n#define A733_PB_BASE 0u\n'
layout = re.search(r'struct a733_i2s0_diag_s\n\{.*?\n\};', driver, re.S)[0]
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
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <sys/a733_audio.h>
#define OK 0
#define TICK2MSEC(t) (t)
#define MSEC2TICK(t) (t)
typedef int mutex_t;
struct file { size_t position; };
static bool busy;
static int nxmutex_trylock(int *lock) { (void)lock; return busy ? -EBUSY : 0; }
static void nxmutex_unlock(int *lock) { (void)lock; }
static int a733_i2s0_capture_into(unsigned ms,bool raw,unsigned slot,unsigned edge,
                                unsigned pull,bool matrix,bool ics,bool voice,
                                unsigned gain,struct a733_audio_capture_s *out)
{ (void)ms;(void)raw;(void)slot;(void)edge;(void)pull;(void)matrix;(void)ics;(void)voice;(void)gain;(void)out; return -ENOTSUP; }
static size_t strlcpy(char *d,const char *s,size_t n)
{ size_t l=strlen(s); if(n) { size_t c=l<n-1?l:n-1; memcpy(d,s,c); d[c]=0; } return l; }
'''
stubs = r'''
static struct a733_i2s0_diag_s g_i2s0;
static unsigned char contents[2000000];
static size_t file_size, pcm_offset;
static unsigned int rate, channels, volume, writes, consumed, queued, allocations;
static unsigned int opens, closes, starts, hw_calls, extra_ms;
static bool active, fail_alloc, stuck_space, stuck_drain, tone_mode;
static int fs_fault, tx_fault, clock_scale;
static uint32_t regs[0x100/4], pb[0x40/4];
static void *kmm_malloc(size_t n) { if(fail_alloc) return NULL; void *p=malloc(n); if(p) allocations++; return p; }
static void kmm_free(void *p) { if(p) { allocations--; free(p); } }
static int file_open(struct file *f,const char *p,int flags,int mode)
{ (void)p; (void)mode; assert(!active && flags==O_RDONLY); opens++; f->position=0; return fs_fault==1?-ENOENT:0; }
static int file_fstat(struct file *f,struct stat *s)
{ (void)f; if(fs_fault==2) return -EIO; memset(s,0,sizeof(*s)); s->st_mode=0100000; s->st_size=file_size; if(fs_fault==3) s->st_mode=0020000; return 0; }
static ssize_t file_read(struct file *f,void *p,size_t n)
{
  assert(!active); /* prove all SD reads finish before clocks start */
  if(fs_fault==4) return -EIO;
  if(fs_fault==5) return 0;
  if(n>7) n=7; /* force short-read loops */
  if(f->position>=file_size) return 0;
  if(n>file_size-f->position) n=file_size-f->position;
  memcpy(p,contents+f->position,n); f->position+=n; return n;
}
static off_t file_seek(struct file *f,off_t n,int whence)
{ assert(!active && whence==SEEK_SET); if(fs_fault==6) return -EIO; assert(n>=0); f->position=n; return n; }
static int file_close(struct file *f)
{ (void)f; assert(!active); closes++; return fs_fault==7?-EIO:0; }
static clock_t clock_systime_ticks(void)
{ return (consumed*1000u/48000u)*clock_scale+extra_ms; }
static uint32_t getreg32(uintptr_t a)
{
  if(a-A733_PB_BASE<sizeof(pb))return pb[(a-A733_PB_BASE)/4];
  unsigned int r=a-A733_I2S0_BASE; assert(r<0x100);
  if(r==I2S_FIFOSTA) {
    if(stuck_space || (stuck_drain && writes>=9600)) { extra_ms++; return 0; }
    if(active && queued>=2) { queued-=2; consumed++; }
    return (128u-queued)<<16;
  }
  if(r==I2S_TXCNT) return consumed;
  if(r==I2S_INTSTA) return writes>=512 ? tx_fault : 0;
  return regs[r/4];
}
static void putreg32(uint32_t v,uintptr_t a)
{
  if(a-A733_PB_BASE<sizeof(pb)) {
    assert(!active);
    if(a==A733_PB_BASE && ((v>>28)&15u)==1)assert(!(pb[0x10/4]&(1u<<7)));
    pb[(a-A733_PB_BASE)/4]=v;return;
  }
  unsigned int r=a-A733_I2S0_BASE; assert(r<0x100);
  if(r==I2S_TXFIFO) { assert(tone_mode && queued<128); queued++; writes++; return; }
  regs[r/4]=v;
  if(r==I2S_CTL) { active=(v&I2S_CTL_TXEN)!=0; if(active) { starts++; assert((tone_mode || queued>0) && ((pb[0]>>28)&15u)==3); } }
  if(r==I2S_FIFOCTL && (v&I2S_FIFOCTL_FTX))queued=0;
  if(r==I2S_TXCNT) consumed=0;
}
static void putreg16(uint16_t v,uintptr_t a)
{
  assert(a==A733_I2S0_BASE+I2S_TXFIFO && queued<128);
  assert((regs[I2S_FMT0/4] & 0x70u)==I2S_FMT0_RES_16);
  if(tone_mode) { queued++; writes++; return; }
  unsigned int frame=writes/2/(48000/rate);
  unsigned int channel=channels==1?0:writes%2;
  size_t i=pcm_offset+(frame*channels+channel)*2;
  int16_t input=(int16_t)(contents[i] | (unsigned int)contents[i+1]<<8);
  int16_t expected=(int16_t)((int32_t)input*(int32_t)volume/100);
  assert(v==(uint16_t)expected);
  queued++; writes++;
}
static void up_udelay(unsigned int us) { assert(us==50); }
static void up_mdelay(unsigned int ms) { extra_ms+=ms; }
static void a733_i2s0_probe(void) {}
static int a733_i2s0_hw_initialize(void) { hw_calls++; return fs_fault==8?-EIO:0; }
'''
tests = r'''
static void le16(unsigned char *p,unsigned int v) { p[0]=v; p[1]=v>>8; }
static void le32(unsigned char *p,uint32_t v) { le16(p,v); le16(p+2,v>>16); }
static void fixture(unsigned int hz,unsigned int ch,unsigned int frames)
{
  assert(allocations==0);
  memset(&g_i2s0,0,sizeof(g_i2s0)); g_i2s0.playback_volume=volume=25;
  rate=hz; channels=ch; writes=consumed=queued=0; opens=closes=starts=hw_calls=extra_ms=0;
  fs_fault=tx_fault=0; fail_alloc=stuck_space=stuck_drain=active=false; clock_scale=1;
  tone_mode=false;
  memset(regs,0,sizeof(regs)); memset(contents,0,sizeof(contents));
  for(unsigned i=0;i<sizeof(pb)/sizeof(pb[0]);i++)pb[i]=0xa5a5a5a5;
  pcm_offset=44; file_size=44+frames*ch*2;
  memcpy(contents,"RIFF",4); le32(contents+4,file_size-8); memcpy(contents+8,"WAVEfmt ",8);
  le32(contents+16,16); le16(contents+20,1); le16(contents+22,ch);
  le32(contents+24,hz); le32(contents+28,hz*ch*2); le16(contents+32,ch*2); le16(contents+34,16);
  memcpy(contents+36,"data",4); le32(contents+40,frames*ch*2);
  for(unsigned int i=0;i<frames*ch;i++) le16(contents+44+i*2,(uint16_t)(int16_t)(((int)i%200-100)*200));
}
static void rejected(void)
{ assert(a733_i2s0_play_wav("/data/test.wav")<0); assert(!active && allocations==0 && starts==0); }
int main(void)
{
  {
    fixture(16000,1,1600);
    struct a733_audio_play_s input={A733_AUDIO_ABI,16000,1,3200,contents+44,25};
    assert(a733_i2s0_ioctl(NULL,A733_AUDIO_PLAY,(unsigned long)&input)==0);
    assert(writes==9600 && opens==0 && closes==0 && allocations==0);
    assert(g_i2s0.play_status==0 && !active);
    fixture(16000,1,1600); input.volume=101;
    assert(a733_i2s0_ioctl(NULL,A733_AUDIO_PLAY,(unsigned long)&input)==-EINVAL && starts==0);
    input.volume=25; input.bytes=3201;
    assert(a733_i2s0_ioctl(NULL,A733_AUDIO_PLAY,(unsigned long)&input)==-EINVAL && starts==0);
    input.bytes=3200; busy=true;
    assert(a733_i2s0_ioctl(NULL,A733_AUDIO_PLAY,(unsigned long)&input)==-EBUSY);
    busy=false;
    struct a733_audio_file_s file={A733_AUDIO_ABI,25,"/data/test.wav"};
    assert(a733_i2s0_ioctl(NULL,A733_AUDIO_FILE,(unsigned long)&file)==0 && opens==1);
  }
  unsigned int rates[]={8000,16000,24000,48000}, volumes[]={0,25,100};
  for(unsigned int r=0;r<4;r++) for(unsigned int ch=1;ch<=2;ch++)
    for(unsigned int v=0;v<3;v++) {
      fixture(rates[r],ch,rates[r]/10); volume=g_i2s0.playback_volume=volumes[v];
      assert(a733_i2s0_play_wav("/data/test.wav")==0);
      assert(writes==9600 && consumed==4800 && starts==1 && !active);
      assert(allocations==0 && opens==1 && closes==1 && g_i2s0.play_sent==4800);
      assert(g_i2s0.play_elapsed==100 && g_i2s0.plays==1 && g_i2s0.play_status==0);
    }
  fixture(48000,1,1); assert(a733_i2s0_play_wav("/data/short.wav")==0 && writes==2);
  /* Preserve pin state across repeated playback. Each operation must restore
   * DOUT0 before TX and park LOW after drain, including the one-frame path. */
  for(unsigned n=0;n<20;n++) {
    writes=consumed=queued=extra_ms=0;
    assert(a733_i2s0_play_wav("/data/short.wav")==0);
    assert(((pb[0]>>28)&15u)==1 && !(pb[0x10/4]&(1u<<7)) && !active);
  }
  for(unsigned mode=0;mode<2;mode++) {
    fixture(48000,1,4800);tone_mode=true;
    assert(a733_i2s0_tone(1000,100,mode==0)==0);
    assert(writes==9600 && starts==1 && !active);
    assert(((pb[0]>>28)&15u)==1 && !(pb[0x10/4]&(1u<<7)));
    writes=consumed=queued=extra_ms=starts=0;stuck_space=true;
    assert(a733_i2s0_tone(1000,100,mode==0)==-ETIMEDOUT && !active);
    assert(((pb[0]>>28)&15u)==1 && !(pb[0x10/4]&(1u<<7)));
  }
  fixture(16000,1,80000); assert(a733_i2s0_play_wav("/data/voice.wav")==0);
  assert(g_i2s0.play_elapsed==5000 && writes==480000);
  /* Arbitrary odd-sized RIFF metadata, including its pad byte. */
  fixture(48000,1,4800);
  memmove(contents+46,contents+36,file_size-36); memcpy(contents+36,"JUNK",4); le32(contents+40,1);
  contents[44]=42; contents[45]=0; file_size+=10; pcm_offset+=10; le32(contents+4,file_size-8);
  assert(a733_i2s0_play_wav("/data/junk.wav")==0);
  unsigned int out;
  assert(a733_play_parse_volume("0",&out)==0 && out==0);
  assert(a733_play_parse_volume("100",&out)==0 && out==100);
  const char *bad[]={"","-1","+1"," 25","25 ","101","9999999999999","25x"};
  for(unsigned int i=0;i<sizeof(bad)/sizeof(bad[0]);i++) assert(a733_play_parse_volume(bad[i],&out)<0);
  for(unsigned int fault=1;fault<=8;fault++) { fixture(48000,1,4800); fs_fault=fault; rejected(); }
  fixture(48000,1,4800); fail_alloc=true; rejected();
  for(unsigned int fault=0;fault<14;fault++) {
    fixture(48000,1,4800);
    if(fault==0) contents[0]='X';
    if(fault==1) le32(contents+4,UINT32_MAX);
    if(fault==2) le16(contents+20,3);
    if(fault==3) le16(contents+22,3);
    if(fault==4) le32(contents+24,44100);
    if(fault==5) le16(contents+34,24);
    if(fault==6) le16(contents+32,4);
    if(fault==7) le32(contents+28,1);
    if(fault==8) le32(contents+40,1);
    if(fault==9) le32(contents+40,0);
    if(fault==10) le32(contents+40,UINT32_MAX);
    if(fault==11) file_size=11;
    if(fault==12) memcpy(contents+12,"data",4);
    if(fault==13) memcpy(contents+36,"JUNK",4);
    rejected(); assert(hw_calls==0);
  }
  fixture(48000,1,4800); assert(a733_i2s0_play_wav("/dev/zero")==-EINVAL && opens==0);
  fixture(48000,1,4800); stuck_space=true;
  assert(a733_i2s0_play_wav("/data/test.wav")==-ETIMEDOUT && !active && allocations==0);
  fixture(48000,1,4800); stuck_drain=true;
  assert(a733_i2s0_play_wav("/data/test.wav")==-ETIMEDOUT && !active && allocations==0);
  fixture(48000,1,4800); tx_fault=1u<<6;
  assert(a733_i2s0_play_wav("/data/test.wav")==-EPIPE && !active && allocations==0);
  fixture(48000,1,4800); clock_scale=0;
  assert(a733_i2s0_play_wav("/data/test.wav")==-EIO && !active && allocations==0);
  puts("PASS: production WAV parsing/short reads, 4 rates x mono/stereo x 3 volumes, signed half-word TX, repeat/no pitch shift, prefill/drain, time/error gates, bad headers/paths/FS/alloc, cleanup; NOT hardware acceptance");
  puts("PASS: PB7 LOW before GPIO-output/RX, TX mux restoration after prefill, 20 repeat plays, S16/S32 tone/timeout park LOW; acoustic silence NOT hardware-tested");
}
'''
with tempfile.TemporaryDirectory(prefix='a733-playback-') as temporary:
    cfile, binary = Path(temporary)/'test.c', Path(temporary)/'test'
    cfile.write_text('\n'.join((prefix, constants, layout, stubs,
        function('a733_pb_pinmux'),function('a733_i2s0_dout_quiet'),
        function('a733_i2s0_stream_stop'),
        function('a733_i2s0_wait_tx_space'),
        function('a733_i2s0_set_sample_resolution'),
        function('a733_i2s0_set_clock_polarity'),function('a733_i2s0_tone'), playback,
        function('a733_i2s0_ioctl'), tests)))
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra',
                    '-Werror', '-fsanitize=address,undefined', '-g',
                    '-I', str(root/'board/a733-cubie-a7z/openvela-overlay/nuttx/include'),
                    '-o', str(binary), str(cfile)], check=True)
    subprocess.run([str(binary)], check=True)
