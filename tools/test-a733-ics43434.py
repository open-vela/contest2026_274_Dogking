#!/usr/bin/env python3
"""Compile production capture/control helpers against an adversarial host MMIO/FS.

This proves software framing, error gates and commit behavior, NOT electrical
timing, real silicon FIFO ordering, microphone health or acoustic acceptance.
"""
import os
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733/a733_i2s0_audio.c'
source = SOURCE.read_text()


def function(name):
    text = SOURCE.with_name('a733_i2s0_playback.inc').read_text() if name == 'a733_play_parse_volume' else source
    match = re.search(r'^static [^;{]*\b' + name + r'\([^;]*?\n\{', text, re.M)
    if not match:
        raise AssertionError(name)
    end = text.index('\n}', match.end()) + 2
    return text[match.start():end]


constants = '\n'.join(re.findall(r'^#define .*?(?<!\\)$', source, re.M))
# Capture the one multiline bank-address macro as well.
constants += '\n#define A733_PB_BASE (A733_PIO_BASE + A733_PIO_FIRST_BANK + A733_PIO_STRIDE)\n'
layout = re.search(r'struct a733_i2s0_diag_s\n\{.*?\n\};', source, re.S)[0]
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
#define OK 0
#define TICK2MSEC(t) (t)
typedef int mutex_t;
struct file { int fd; off_t f_pos; };
static int nxmutex_lock(mutex_t *lock) { (void)lock; return 0; }
static bool busy;
static int nxmutex_trylock(mutex_t *lock) { (void)lock; return busy ? -EBUSY : 0; }
static void nxmutex_unlock(mutex_t *lock) { (void)lock; }
static size_t strlcpy(char *dest, const char *src, size_t size)
{ size_t n = strlen(src); if(size) { size_t c=n<size-1?n:size-1; memcpy(dest,src,c); dest[c]=0; } return n; }
'''
stubs = r'''
static struct a733_i2s0_diag_s g_i2s0;
struct a733_wav_s { unsigned char *pcm; uint32_t bytes, rate, channels, frames; };
static int a733_i2s0_play_pcm(const struct a733_wav_s *wav) { (void)wav; return -ENOTSUP; }
static uint32_t regs[0x100 / 4], pb[0x40 / 4];
#define pull_register pb[0x30/4]
static unsigned int rx_words, hw_frames, timeout_at, allocations;
static bool counter_per_word;
static unsigned int counter_stall_at;
static int clock_scale = 1, counter_delta = 1, irq_fault, fs_fault;
static bool streaming;
static unsigned char pending[4000000], committed[4000000];
static size_t pending_size, committed_size;
static char pending_path[128], committed_path[128];
static bool pending_exists;
static uint32_t getreg32(uintptr_t address)
{
  if (address >= A733_PB_BASE && address < A733_PB_BASE + sizeof(pb)) return pb[(address-A733_PB_BASE)/4];
  assert(address >= A733_I2S0_BASE && address < A733_I2S0_BASE + 0x100);
  unsigned int reg = (unsigned int)(address - A733_I2S0_BASE);
  if (reg == I2S_RXFIFO) {
    assert(streaming);
    unsigned int channels = ((regs[I2S_CHCFG/4] >> 4) & 15u) + 1;
    unsigned int slot = channels == 2 ? rx_words % 2 : regs[I2S_RXCHMAP3/4] & 15u;
    rx_words++;
    if ((!counter_stall_at || rx_words < counter_stall_at) &&
        (counter_per_word || rx_words % channels == 0)) hw_frames++;
    return slot == 0 ? 0x00123400u : 0xffedcc00u;
  }
  if (reg == I2S_RXCNT) return hw_frames + counter_delta;
  if (reg == I2S_INTSTA) return irq_fault;
  return regs[reg/4];
}
static void putreg32(uint32_t value, uintptr_t address)
{
  if (address >= A733_PB_BASE && address < A733_PB_BASE + sizeof(pb)) {
    assert(!streaming);
    if(address==A733_PB_BASE && ((value>>28)&15u)==1)assert((pb[0x10/4]&(1u<<7))==0);
    pb[(address-A733_PB_BASE)/4]=value; return;
  }
  assert(address >= A733_I2S0_BASE && address < A733_I2S0_BASE + 0x100);
  unsigned int reg = (unsigned int)(address - A733_I2S0_BASE);
  if(reg == I2S_RXCNT) { rx_words=0; hw_frames=0; }
  if(reg == I2S_CTL) {
    streaming=(value & I2S_CTL_RXEN) != 0;
    if(streaming) {
      assert(((pb[0]>>28)&15u)==1 && !(pb[0x10/4]&(1u<<7)));
      assert(!(value&(I2S_CTL_TXEN|I2S_CTL_SDO0_EN)));
    }
  }
  regs[reg/4] = value;
}
static clock_t clock_systime_ticks(void)
{
  unsigned int channels = ((regs[I2S_CHCFG/4] >> 4) & 15u) + 1;
  return rx_words * 1000u / (48000u * channels) * clock_scale;
}
static int a733_i2s0_hw_initialize(void) { return 0; }
static int a733_i2s0_tone(unsigned int a,unsigned int b,bool s16)
{ (void)a; (void)b; (void)s16; return 0; }
static void a733_i2s0_probe(void) {}
static int a733_i2s0_play_wav(const char *path) { (void)path; return -ENOTSUP; }
static int a733_i2s0_wait_rx_data(uint32_t *status)
{
  if(timeout_at && rx_words >= timeout_at) return -ETIMEDOUT;
  *status=I2S_FIFOSTA_RXA | 0x80u | 2u;
  return 0;
}
static void *kmm_malloc(size_t size) { void *p=malloc(size); if(p) allocations++; return p; }
static void kmm_free(void *p) { if(p) allocations--; free(p); }
static int nx_unlink(const char *path)
{ if(pending_exists && strcmp(path,pending_path)==0) pending_exists=false; return 0; }
static int file_open(struct file *f, const char *path, int flags, int mode)
{
  (void)f; (void)flags; (void)mode;
  if(fs_fault==1) return -ENOSPC;
  strlcpy(pending_path,path,sizeof(pending_path)); pending_exists=true; pending_size=0; return 0;
}
static ssize_t file_write(struct file *f,const void *data,size_t size)
{
  (void)f;
  if(fs_fault==2 && pending_size>100) return -ENOSPC;
  if(fs_fault==5) return 0;
  if(size>7) size=7;  /* force every header/payload through short writes */
  assert(pending_size+size<=sizeof(pending));
  memcpy(pending+pending_size,data,size); pending_size+=size; return size;
}
static int file_close(struct file *f) { (void)f; return fs_fault==3 ? -EIO : 0; }
static int test_rename(const char *from,const char *to)
{
  assert(pending_exists && strcmp(from,pending_path)==0);
  if(fs_fault==4) { errno=EIO; return -1; }
  memcpy(committed,pending,pending_size); committed_size=pending_size;
  strlcpy(committed_path,to,sizeof(committed_path)); pending_exists=false; return 0;
}
#define rename test_rename
#define get_errno() errno
'''
tests = r'''
static uint32_t le32(const unsigned char *p)
{ return p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static void reset_test(void)
{
  memset(regs,0,sizeof(regs)); regs[I2S_CHCFG/4]=0xa5a50201;
  for(unsigned i=0;i<sizeof(pb)/sizeof(pb[0]);i++)pb[i]=0xa5a5a5a5;
  timeout_at=0; clock_scale=1; counter_delta=1; irq_fault=fs_fault=0;
  counter_per_word=false; counter_stall_at=0;
  assert(allocations==0); pending_exists=false;
}
static void assert_uncommitted(void)
{
  assert(!g_i2s0.last_capture_committed && !streaming && !pending_exists);
  assert(committed_size==4 && memcmp(committed,"SAFE",4)==0);
  assert(allocations==0);
}
int main(void)
{
  {
    reset_test(); committed_size=4; memcpy(committed,"SAFE",4);
    unsigned char pcm[9600];
    struct a733_audio_capture_s request={A733_AUDIO_ABI,100,5,sizeof(pcm),pcm,999};
    assert(a733_i2s0_ioctl(NULL,A733_AUDIO_CAPTURE,(unsigned long)&request)==0);
    assert(request.bytes==9600 && g_i2s0.capture_gain==5);
    assert((pb[0]&0x0fffffffu)==(0xa5a5a5a5u&0x0fffffffu));
    assert((pb[0x10/4]&~(1u<<7))==(0xa5a5a5a5u&~(1u<<7)));
    assert(((pb[0]>>28)&15u)==1 && !(pb[0x10/4]&(1u<<7)));
    assert(!g_i2s0.last_capture_committed && strcmp(g_i2s0.last_capture_path,"RAM (no file)")==0);
    assert(committed_size==4 && memcmp(committed,"SAFE",4)==0 && !streaming && allocations==0);
    assert((int16_t)(pcm[0] | (unsigned)pcm[1]<<8)==0x1234*5);
    request.gain=17;
    assert(a733_i2s0_ioctl(NULL,A733_AUDIO_CAPTURE,(unsigned long)&request)==-EINVAL && request.bytes==0);
    busy=true;
    assert(a733_i2s0_ioctl(NULL,A733_AUDIO_CAPTURE,(unsigned long)&request)==-EBUSY);
    busy=false;
    assert(a733_i2s0_ioctl(NULL,123,(unsigned long)&request)==-ENOTTY);
  }
  unsigned int duration;
  unsigned int gain;
  assert(a733_parse_voice("100",&duration,&gain)==0 && duration==100 && gain==5);
  assert(a733_parse_voice("10000,16",&duration,&gain)==0 && duration==10000 && gain==16);
  assert(a733_parse_voice("5000,1",&duration,&gain)==0 && duration==5000 && gain==1);
  const char *bad_voice[]={"", "99", "10001", "5000,", "5000,0", "5000,17",
    "5000,-4", "5000,+4", "5000, 4", "5000,4x", "5000,4,1", "-5000,4",
    "5000x,4", "5000,4294967296", "99999999999999999999,4"};
  for(unsigned int i=0;i<sizeof(bad_voice)/sizeof(bad_voice[0]);i++)
    assert(a733_parse_voice(bad_voice[i],&duration,&gain)<0);

  /* Exhaust every signed S16 value for every supported gain.  Validate
   * asymmetric rails, no wrap, exact clip counts and unity identity.
   */
  int16_t *ramp=malloc(65536*sizeof(*ramp));
  assert(ramp);
  for(unsigned int g=1;g<=16;g++) {
    uint32_t clipped=0;
    for(unsigned int i=0;i<65536;i++) ramp[i]=(int16_t)((int32_t)i-32768);
    g_i2s0.capture_clipped=0;
    a733_i2s0_apply_gain(ramp,65536,g);
    for(unsigned int i=0;i<65536;i++) {
      int32_t expected=((int32_t)i-32768)*(int32_t)g;
      if(expected>32767) { expected=32767; clipped++; }
      if(expected< -32768) { expected= -32768; clipped++; }
      assert(ramp[i]==expected);
    }
    assert(g_i2s0.capture_clipped==clipped);
  }
  free(ramp);
  /* Exact v146 user fixture: software words=489600, hw=244801 at 5100ms.
   * RXCNT's frame-candidate unit is distinct from FIFO words. This alone
   * does not prove acoustic content or the physical clock frequency.
   */
  assert(a733_i2s0_check_rx_counter(489600,244801,2)==0);
  assert(a733_i2s0_check_rx_counter(489600,244799,2)==-EIO);
  assert(a733_i2s0_check_rx_counter(489600,489601,2)==-EOVERFLOW);
  assert(a733_i2s0_check_rx_counter(489600,UINT32_MAX,2)==-EOVERFLOW);
  assert(a733_i2s0_check_rx_counter(489601,244801,2)==-EINVAL);
  assert(a733_i2s0_check_rx_counter(1,1,0)==-EINVAL);
  const char *bad[]={"", "-5000", "+5000", " 5000", "5000 ","5000x", "99", "10001", "4294967296", "99999999999999999999"};
  assert(a733_parse_duration("100",&duration)==0 && duration==100);
  assert(a733_parse_duration("10000",&duration)==0 && duration==10000);
  for(unsigned int i=0;i<sizeof(bad)/sizeof(bad[0]);i++)
    assert(a733_parse_duration(bad[i],&duration)<0);

  reset_test();
  uint32_t unchanged=regs[I2S_CHCFG/4] & ~(15u<<4);
  a733_i2s0_configure_rx(true,2);
  assert(regs[I2S_CHCFG/4]==(unchanged | 0x10u));
  assert(regs[I2S_RXCHSEL/4]==0x00110000u);
  assert(regs[I2S_RXCHMAP3/4]==0x00000100u); /* NOT 0x10 */
  for(unsigned int i=0;i<3;i++) assert(regs[(I2S_RXCHMAP0+4*i)/4]==0);
  a733_i2s0_configure_rx(false,1);
  assert(regs[I2S_CHCFG/4]==unchanged && regs[I2S_RXCHSEL/4]==0x00100000u);
  assert(regs[I2S_RXCHMAP3/4]==1);

  for(unsigned int slot=0;slot<3;slot++) {
    reset_test();
    assert(a733_i2s0_capture(100,false,slot,0,2,false,true)==0);
    unsigned int channels=slot==2?2:1;
    assert(g_i2s0.capture_rx_channels==2 && g_i2s0.capture_file_channels==channels);
    assert(g_i2s0.capture_target_rx==19200 && g_i2s0.capture_consumed_rx==19200);
    assert(g_i2s0.capture_expected_counter==9600 && g_i2s0.rx_count==9601);
    assert(g_i2s0.capture_elapsed_ms==200 && g_i2s0.samples==4800*channels);
    assert(!streaming && g_i2s0.last_capture_committed && allocations==0);
    assert(committed_size==44+4800*channels*2);
    assert(memcmp(committed,"RIFF",4)==0 && memcmp(committed+8,"WAVEfmt ",8)==0);
    assert(le32(committed+4)==committed_size-8 && le32(committed+40)==committed_size-44);
    assert(committed[22]==channels && le32(committed+24)==48000);
    assert(le32(committed+28)==48000*channels*2 && committed[32]==channels*2);
    for(unsigned int i=0;i<4800*channels;i++) {
      int16_t sample=(int16_t)(committed[44+i*2] | committed[45+i*2]<<8);
      assert(sample==(slot==2 ? (i%2 ? -0x1234 : 0x1234) : (slot==0 ? 0x1234 : -0x1234)));
    }
    assert(strstr(committed_path, slot==0?"ics43434-left.wav":slot==1?"ics43434-right.wav":"ics43434-stereo.wav"));
  }
  reset_test();
  assert(a733_i2s0_capture(5000,false,2,0,2,false,true)==0);
  assert(committed_size==960044 && g_i2s0.capture_elapsed_ms==5100);
  assert(g_i2s0.capture_consumed_rx==489600 && g_i2s0.samples==480000);
  assert(g_i2s0.capture_expected_counter==244800 && g_i2s0.rx_count==244801);
  assert(g_i2s0.capture_counter_points==6 && g_i2s0.capture_counter_status==0);
  for(unsigned int i=0;i<6;i++) {
    assert(g_i2s0.capture_counter_words[i]==9600+96000*i);
    assert(g_i2s0.capture_counter_hw[i]==4801+48000*i);
  }
  for(unsigned int mode=0;mode<2;mode++) {
    reset_test(); committed_size=4; memcpy(committed,"SAFE",4);
    if(mode==0) counter_per_word=true; else counter_stall_at=30000;
    assert(a733_i2s0_capture(5000,false,2,0,2,false,true)<0);
    assert_uncommitted();
  }
  reset_test();
  assert(a733_i2s0_capture(100,true,2,0,2,false,true)==0);
  assert(committed_size==38400 && le32(committed)==0x00123400u && le32(committed+4)==0xffedcc00u);
  assert(strstr(committed_path,"ics43434-stereo-s24.bin"));
  reset_test();
  assert(a733_i2s0_capture(100,false,1,0,2,false,false)==0);
  assert(committed_size==9644 && g_i2s0.capture_consumed_rx==9600);
  assert(strstr(committed_path,"inmp441.wav"));

  for(unsigned int fault=0;fault<10;fault++) {
    reset_test(); committed_size=4; memcpy(committed,"SAFE",4);
    if(fault==0) timeout_at=10000;
    if(fault==1) counter_delta=-1;
    if(fault==2) counter_delta=129;
    if(fault==3) clock_scale=0;
    if(fault==4) clock_scale=2;
    if(fault==5) irq_fault=I2S_INTSTA_RX_OVERRUN;
    if(fault>=6) fs_fault=(int)fault-5;
    assert(a733_i2s0_capture(100,false,2,0,2,false,true)<0);
    assert_uncommitted();
  }
  reset_test(); fs_fault=5; committed_size=4; memcpy(committed,"SAFE",4);
  assert(a733_i2s0_capture(100,false,2,0,2,false,true)==-EIO);
  assert_uncommitted();
  reset_test();
  assert(a733_i2s0_capture(10001,false,2,0,2,false,true)==-ERANGE);
  assert(a733_i2s0_capture(100,false,2,0,2,false,false)==-ERANGE);
  assert(a733_i2s0_capture(100,true,0,0,2,false,true)==-ERANGE);
  struct file control={0};
  assert(a733_i2s0_write(&control,"volume=25",9)==9 && g_i2s0.playback_volume==25);
  assert(a733_i2s0_write(&control,"volume=101",10)<0 && g_i2s0.playback_volume==25);
  assert(a733_i2s0_write(&control,"play=/data/x.wav",16)==-ENOTSUP); /* dispatch to play stub */
  for(unsigned int g=1;g<=16;g++) {
    reset_test();
    assert(a733_i2s0_capture_ex(100,false,0,0,2,false,true,true,g)==0);
    assert(g_i2s0.capture_voice && g_i2s0.capture_processed);
    assert(g_i2s0.capture_gain==g && g_i2s0.capture_input_min==0x1234 &&
           g_i2s0.capture_input_max==0x1234);
    assert(g_i2s0.capture_clipped==(g>=8 ? 4800u : 0u));
    assert(committed_size==9644 && committed[22]==1);
    assert(strcmp(committed_path,"/data/ics43434-voice.wav")==0);
    assert(g_i2s0.capture_rx_channels==2 && g_i2s0.capture_consumed_rx==19200);
    int32_t expected=0x1234*(int32_t)g;
    if(expected>32767) expected=32767;
    for(unsigned int i=0;i<4800;i++)
      assert((int16_t)(committed[44+i*2] | committed[45+i*2]<<8)==expected);
    assert(g_i2s0.capture_min==expected && !streaming && allocations==0);
    /* The original API restores unprocessed amplitude and its original path. */
    assert(a733_i2s0_capture(100,false,0,0,2,false,true)==0);
    assert(!g_i2s0.capture_voice && !g_i2s0.capture_processed);
    assert(g_i2s0.capture_gain==1 && g_i2s0.capture_clipped==0);
    assert(g_i2s0.capture_min==0x1234 && strstr(committed_path,"ics43434-left.wav"));
  }
  for(unsigned int fault=0;fault<13;fault++) {
    reset_test(); committed_size=4; memcpy(committed,"SAFE",4);
    if(fault==0) timeout_at=10000;
    if(fault==1) counter_delta=-1;
    if(fault==2) counter_delta=129;
    if(fault==3) clock_scale=0;
    if(fault==4) clock_scale=2;
    if(fault==5) irq_fault=I2S_INTSTA_RX_OVERRUN;
    if(fault>=6 && fault<=10) fs_fault=(int)fault-5;
    if(fault==11) counter_per_word=true;
    if(fault==12) counter_stall_at=10000;
    assert(a733_i2s0_capture_ex(100,false,0,0,2,false,true,true,4)<0);
    assert_uncommitted();
    if(fault<6 || fault>10) assert(!g_i2s0.capture_processed);
  }
  reset_test();
  assert(a733_i2s0_capture_ex(5000,false,0,0,2,false,true,true,4)==0);
  assert(committed_size==480044 && g_i2s0.samples==240000);
  {
    struct file rf={0}; char full_report[4096]={0};
    ssize_t rn=a733_i2s0_read(&rf,full_report,sizeof(full_report)-1);
    assert(rn>0 && rn<3583 &&
           strstr(full_report,"(sign-extended S24 FIFO words)\n"));
  }
  assert(g_i2s0.capture_elapsed_ms==5100 && g_i2s0.capture_counter_points==6);
  assert(g_i2s0.capture_consumed_rx==489600 && g_i2s0.rx_count==244801);
  reset_test();
  assert(a733_i2s0_write(&control,"icsvoice=100\n",13)==13);
  assert(g_i2s0.capture_gain==5 && g_i2s0.last_capture_committed);
  assert(a733_i2s0_write(&control,"\n",1)==1 && g_i2s0.last_capture_committed);
  assert(a733_i2s0_write(&control,"icsvoice=100,8",14)==14 && g_i2s0.capture_gain==8);
  assert(a733_i2s0_write(&control,"icsvoice=100,1",14)==14 && g_i2s0.capture_gain==1);
  for(unsigned int i=0;i<sizeof(bad_voice)/sizeof(bad_voice[0]);i++) {
    char bad_command[64];
    reset_test(); committed_size=4; memcpy(committed,"SAFE",4);
    snprintf(bad_command,sizeof(bad_command),"icsvoice=%s",bad_voice[i]);
    assert(a733_i2s0_write(&control,bad_command,strlen(bad_command))<0);
    assert_uncommitted();
    assert(!g_i2s0.capture_processed);
  }
  reset_test();
  assert(a733_i2s0_capture_ex(100,true,0,0,2,false,true,true,4)==-ERANGE);
  assert(a733_i2s0_capture_ex(100,false,1,0,2,false,true,true,4)==-ERANGE);
  assert(a733_i2s0_capture_ex(100,false,0,0,2,false,true,true,0)==-ERANGE);
  assert(a733_i2s0_capture_ex(100,false,0,0,2,false,true,false,4)==-ERANGE);
  const char *commands[]={"icscapture=100\n", "icsleft=100\r\n", "icsright=100", "icsraw=100"};
  for(unsigned int i=0;i<4;i++) {
    reset_test();
    assert(a733_i2s0_write(&control,commands[i],strlen(commands[i]))==(ssize_t)strlen(commands[i]));
    assert(g_i2s0.capture_ics && g_i2s0.last_capture_committed);
    assert(g_i2s0.capture_slot==(i==1?0u:i==2?1u:2u));
    assert(g_i2s0.capture_rx_channels==2 && allocations==0);
    assert(a733_i2s0_write(&control,"\n",1)==1 && g_i2s0.last_capture_committed);
  }
  reset_test();
  assert(a733_i2s0_write(&control,"icscapture=-100",15)<0 && !g_i2s0.last_capture_committed);
  assert(!streaming && allocations==0);
  assert(a733_i2s0_write(&control,"icscapture=10001",16)<0);
  assert(a733_i2s0_write(&control,"icscapture=100x",15)<0);
  struct file report_file={0}; char report[4096]={0};
  ssize_t n=a733_i2s0_read(&report_file,report,sizeof(report)-1);
  assert(n>0 && n<3583 && strstr(report,"icscapture=<ms>") && strstr(report,"NOT acoustic acceptance"));
  assert(strstr(report,"counter-policy=frame-candidate"));
  assert(strstr(report,"processing: voice=") && strstr(report,"icsvoice=<ms>"));
  assert(strstr(report,"(sign-extended S24 FIFO words)\n")); /* no tail truncation */
  assert(strstr(report,"default x5;") && strstr(report,"driver v162"));
  assert(strstr(report,"PB7=GPIO-low idle/RX"));
  puts("PASS: v150 retains default x5, exhaustive S16 x 16 gains, saturation/clip counts, left mono/header/path, default/custom gain, strict arguments, original amplitude restoration, 13 voice failures preserve old file");
  puts("PASS: actual ICS capture: v146 fixture, frame/FIFO units, six intermediate counter checks, reject wrong scaling/stalled/huge counts; stereo/mono/raw, WAV, time gates, legacy, 13 injected failures preserve old file, no leaks, diagnostics");
}
'''
helpers = ('a733_pb_pull', 'a733_i2s0_set_sample_resolution',
           'a733_i2s0_set_clock_polarity', 'a733_put_le16', 'a733_put_le32',
           'a733_file_write_all', 'a733_i2s0_capture_stats',
           'a733_i2s0_configure_rx', 'a733_i2s0_check_rx_counter',
           'a733_i2s0_record_rx_counter', 'a733_i2s0_capture', 'a733_parse_duration',
           'a733_parse_pair', 'a733_parse_voice', 'a733_parse_capturecfg',
           'a733_play_parse_volume', 'a733_i2s0_write',
           'a733_i2s0_read')
with tempfile.TemporaryDirectory(prefix='a733-ics43434-') as directory:
    cfile = Path(directory) / 'test.c'
    binary = Path(directory) / 'test'
    cfile.write_text('\n'.join((prefix, constants, layout, stubs,
                               function('a733_pb_pinmux'),
                               function('a733_i2s0_dout_quiet'),
                               function('a733_i2s0_stream_stop'),
                               function('a733_i2s0_apply_gain'),
                               *(function(name) for name in helpers[:10]),
                               function('a733_i2s0_capture_into'),
                               function('a733_i2s0_capture_ex'),
                               *(function(name) for name in helpers[10:]),
                               function('a733_i2s0_ioctl'), tests)))
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra',
                    '-Werror', '-fsanitize=undefined,address', '-g',
                    '-I', str(ROOT / 'board/a733-cubie-a7z/openvela-overlay/nuttx/include'),
                    '-o', str(binary), str(cfile)], check=True)
    subprocess.run([str(binary)], check=True)
