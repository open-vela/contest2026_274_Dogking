#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Production UFS wire + driver with hostile MMIO/DMA. Linux host only.

No real device, MMIO, SDK edits, network, media writes or hardware acceptance.
Only include lines are removed from the driver to substitute OS/MMIO shims;
production function bodies, encoder and state machine are compiled unchanged.
"""
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CHIP = ROOT / 'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733'
PREFIX = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <time.h>
#include "a733_ufs_wire.h"
#include "a733_ufs_board.h"
#include "a733_ufs_handoff.h"
#include "a733_ufs_quiesce.h"
#include "a733_ufs_nop.h"
#include "a733_ufs_readcheck.h"
#include "a733_ufs_scratch.h"
#include "a733_ufs_data.h"
#define BIOC_FLUSH 0x000d
/* Historical transport coverage uses a HOST-ONLY permit shim. Production
 * a733_ufs_board_hci_allowed(), linked separately below, ALWAYS denies. */
static bool host_test_permit=true;
static bool host_hci_allowed(void) {return host_test_permit;}
#define a733_ufs_board_hci_allowed host_hci_allowed
#define CONFIG_A733_UFS_READONLY 1
#define NXMUTEX_INITIALIZER 0
#define TICK2MSEC(t) (t)
#define UP_DSB() ((void)0)
typedef int mutex_t;
static bool busy;
static unsigned highalloc;
static int nxmutex_lock(mutex_t *m) {(void)m; return busy ? -EBUSY : 0;}
static int nxmutex_trylock(mutex_t *m) {return nxmutex_lock(m);}
static void nxmutex_unlock(mutex_t *m) {(void)m;}
struct file {off_t f_pos;};
struct inode {int unused;};
struct geometry {bool geo_available,geo_mediachanged,geo_writeenabled;
  blkcnt_t geo_nsectors; blksize_t geo_sectorsize; char geo_model[256];};
struct file_operations {
  ssize_t (*read)(struct file *,char *,size_t);
  ssize_t (*write)(struct file *,const char *,size_t);
};
struct block_operations {
  ssize_t (*read)(struct inode *,unsigned char *,blkcnt_t,unsigned int);
  ssize_t (*write)(struct inode *,const unsigned char *,blkcnt_t,unsigned int);
  int (*geometry)(struct inode *,struct geometry *);
  int (*ioctl)(struct inode *,int,unsigned long);
};
static unsigned registered,block_registered,freed,cleaned,invalidated,writes,hci_reads;
static const struct block_operations *published_ops;
static int block_register_error;
static bool bulk_corrupt,bulk_repeat_corrupt;
static unsigned bulk_reads;
static unsigned fault,commands,delay_ms;
static uint32_t regs[256];
static bool clocks=true;
static bool handoff_profile,drop_handoff_clocks;
static bool drop_quiesce_clocks;
static bool noptrial,drop_nop_clocks,nop_map_fault;
static bool drop_read_clocks;
/* HOST ONLY stand-in. The actual ARM64 AT/PAR helper is compiled separately. */
int a733_ufs_nop_translate(void *context,uintptr_t va,uint64_t *pa)
{(void)context;if(nop_map_fault)return -EFAULT;*pa=va;return 0;}
static clock_t ticks;
static size_t strlcpy(char *d,const char *s,size_t n)
{size_t k=strlen(s); if(n){size_t c=k<n-1?k:n-1;memcpy(d,s,c);d[c]=0;}return k;}
static int register_driver(const char *n,const struct file_operations *f,int m,void *p)
{
 if(!strcmp(n,"/dev/a733-ufs-backup")){assert(f&&f->read&&!f->write&&m==0444&&!p);return 0;}
 assert(!strcmp(n,"/dev/a733-ufs")&&f&&m==0600&&!p);registered++;return 0;
}
static int register_blockdriver(const char *n,const struct block_operations *f,int m,void *p)
{assert((!strcmp(n,"/dev/ufs0")||!strcmp(n,"/dev/ufsdata-ro"))&&f&&m==0444&&!p&&!f->write&&!f->ioctl);
 if(block_register_error)return block_register_error;
 block_registered++;published_ops=f;return 0;}
static void *kmm_memalign(size_t a,size_t n)
{if(highalloc)return (void *)(highalloc==1?UINT64_C(0x100004000):UINT64_C(0xffffc000));
 void *p=mmap(NULL,n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);
 assert(p!=MAP_FAILED&&a==1024);return p;}
static void kmm_free(void *p) {if(!highalloc)assert(!munmap(p,A733_UFS_DMA_SIZE));freed++;}
static void up_clean_dcache(uintptr_t a,uintptr_t b)
{assert(a<b&&!(a%1024)&&b-a==(noptrial?A733_UFS_NOP_ARENA:A733_UFS_DMA_SIZE));cleaned++;}
static void up_invalidate_dcache(uintptr_t a,uintptr_t b)
{assert(a<b&&b-a==(noptrial?A733_UFS_NOP_ARENA:A733_UFS_DMA_SIZE));invalidated++;}
static clock_t clock_systime_ticks(void) {return ticks;}
static void up_udelay(unsigned n) {assert(n==100);ticks++;delay_ms++;}
static uint32_t getreg32(uintptr_t a)
{
  if(handoff_profile && a<0x04520000) {
    static const uint32_t addresses[]={0x02002000,0x020020a0,0x020020a8,0x020020ac,
      0x020025c0,0x020025e0,0x0200259c,0x02002d80,0x02002d84,0x02002d8c,0x02002d90};
    static const uint32_t values[]={0xf8675f00,0xff126310,0,0x30000000,
      0x110103ff,0xf1055803,0x30001,0x81000000,0x80000018,0xf0001,0};
    for(unsigned i=0;i<11;i++)if(a==addresses[i])return clocks?values[i]:0;
  }
  if(handoff_profile && a==0x07090160)return 0x183fb0f7;
  if(handoff_profile && a==0x0709016c)return 0x511;
  if(a==0x02002d8c)return clocks?0xf0001:0;
  if(a==0x02002d80||a==0x02002d84)return clocks?0x80000000:0;
  if(a==0x02002000||a==0x020020a0||a==0x020020a8||a==0x020020ac||
     a==0x020025c0||a==0x020025e0||a==0x0200259c||a==0x02002d90||
     a==0x07090160||a==0x0709016c)return 0;
  assert(clocks&&a>=0x04520000&&a<0x04520400&&!(a%4));hci_reads++;
  if(drop_handoff_clocks)clocks=false;
  return regs[(a-0x04520000)/4];
}
static void putreg32(uint32_t n,uintptr_t a)
{
  assert(clocks);assert(a==0x04520050||a==0x04520054||a==0x04520060||
    a==0x04520020||a==0x04520058||a==0x04520024||a==0x04520080);writes++;
  if(a==0x04520020) {assert(n==1);regs[8]&=~n;return;}
  regs[(a-0x04520000)/4]=n;
  if(drop_quiesce_clocks && a==0x04520024){clocks=false;return;}
  if(drop_nop_clocks && a==0x04520050){clocks=false;return;}
  if(drop_read_clocks && commands==1 && a==0x04520050){clocks=false;return;}
  if(a!=0x04520058)return;
  assert(n==1&&regs[0x60/4]==1&&cleaned>invalidated);
  commands++;
  if(noptrial && !((uint8_t *)(uintptr_t)regs[0x50/4])[A733_UFS_UCD_OFFSET]){
    uintptr_t pa=(uintptr_t)regs[0x50/4];uint8_t *m=(uint8_t *)pa;
    assert(m[A733_UFS_UCD_OFFSET]==0&&m[A733_UFS_UCD_OFFSET+3]==0);
    ufs_le32(m+8,0);m[A733_UFS_RSP_OFFSET]=0x20;m[A733_UFS_RSP_OFFSET+3]=0;
    regs[0x58/4]=0;regs[8]=1;return;
  }
  if(fault==1)return; /* controller never clears doorbell */
  if(fault==2){regs[8]=0x10000;return;}
  uintptr_t pa=(uintptr_t)regs[0x50/4]|(uint64_t)regs[0x54/4]<<32;
  uint8_t *m=(uint8_t *)pa,*cmd=m+A733_UFS_UCD_OFFSET;
  uint8_t *rsp=m+A733_UFS_RSP_OFFSET,*d=m+A733_UFS_DATA_OFFSET;
  unsigned op=cmd[0]?cmd[16]:0;
  assert(op==0||op==0x12||op==0x9e||op==0x88);
  assert(ufs_getle32(m)==(op?0x15000000u:0x11000000u));
  assert(cmd[3]==0);
  ufs_le32(m+8,fault==3?7:0);
  rsp[0]=op?0x21:0x20;rsp[2]=cmd[2];rsp[3]=cmd[3];
  if(fault==4)rsp[3]=1;
  if(fault==5)rsp[7]=2;
  if(fault==6){rsp[1]=0x20;ufs_be32(rsp+12,4);}
  if(op==0x12){d[0]=0;d[4]=31;memset(d+8,' ',28);memcpy(d+8,"HOST MOCK UFS ONLY",18);}
  if(op==0x9e){ufs_be32(d,0);ufs_be32(d+4,33554431);ufs_be32(d+8,fault==7?1024:4096);}
  if(op==0x88){unsigned bytes=ufs_getbe32(cmd+12);memset(d,0x7b,bytes);
    if(bytes==65536){++bulk_reads;if(bulk_corrupt)d[0]^=1;
      if(bulk_repeat_corrupt&&bulk_reads==17)d[20000]^=1;}}
  regs[0x58/4]=0;regs[8]=1;
}
'''
TEST = r'''
#undef a733_ufs_board_hci_allowed
int main(int argc,char **argv)
{
  assert(argc==2);int scenario=atoi(argv[1]);
  regs[0]=0x0100001f;regs[2]=0x300;regs[0x30/4]=0xf;regs[0x34/4]=1;
  assert(a733_ufs_initialize()==0&&registered==1&&!writes&&!hci_reads);
  assert(!a733_ufs_board_hci_allowed()); /* Real production gate, no bypass */
  struct file f={0};char report[3000];
  assert(ufs_diag_read(&f,report,sizeof(report))>0&&!hci_reads);
  if(scenario==0){
    clocks=false;
    assert(ufs_diag_write(&f,"probe\n",6)==-EAGAIN&&!hci_reads&&!writes);
    assert(ufs_diag_write(&f,"attach-ro=0\n",12)==-EAGAIN&&!hci_reads&&!writes);
  }else if(scenario==1){
    regs[0x34/4]=0;assert(ufs_attach(0)==-ENOLINK&&!writes&&!block_registered);
  }else if(scenario==2){
    regs[0x60/4]=1;assert(ufs_attach(0)==-EBUSY&&!writes&&!block_registered);
  }else if(scenario>=3&&scenario<=9){
    fault=scenario-2;assert(ufs_attach(0)<0&&g_ufs.poisoned&&!block_registered);
    unsigned previous=commands;assert(ufs_attach(0)==-EIO&&commands==previous);
    assert(g_ufs.dma&&!freed);assert(delay_ms<=UFS_TIMEOUT_MS);
  }else if(scenario==10){
    assert(ufs_diag_write(&f,"attach-ro=2\n",12)==12&&block_registered==1);
    assert(g_ufs.blocks==33554432&&g_ufs.blocksize==4096&&commands==3);
    struct geometry geo;assert(!ufs_block_geometry(NULL,&geo));
    assert(geo.geo_available&&!geo.geo_writeenabled&&geo.geo_nsectors==33554432);
    unsigned char *buffer=malloc(131072);assert(buffer);
    assert(ufs_block_read(NULL,buffer,3,32)==32&&commands==5);
    for(unsigned i=0;i<131072;i++)assert(buffer[i]==0x7b);
    assert(g_ufs.readbytes==131072);
    assert(ufs_block_read(NULL,buffer,-1,1)==-EINVAL);
    assert(ufs_block_read(NULL,buffer,33554432,1)==-EINVAL);
    assert(ufs_block_read(NULL,buffer,33554432,0)==0);
    assert(ufs_attach(1)==-EBUSY&&commands==5);
    assert(ufs_diag_write(&f,"format\n",7)==-EINVAL&&commands==5);
    assert(ufs_diag_write(&f,"attach-ro=8",11)==-EINVAL);
    assert(ufs_diag_write(&f,"probe\0junk",10)==-EINVAL);
    busy=true;assert(ufs_diag_write(&f,"probe",5)==-EBUSY);busy=false;
    fault=1;assert(ufs_block_read(NULL,buffer,0,1)==-ETIMEDOUT);
    assert(!ufs_block_geometry(NULL,&geo)&&!geo.geo_available);
    assert(ufs_block_read(NULL,buffer,0,1)==-ENODEV);
    free(buffer);
  }else if(scenario==11){
    uint8_t *m=malloc(A733_UFS_DMA_SIZE);assert(m);
    assert(ufs_read_command(NULL,0,0,0,0,0,0,0)==-EINVAL);
    assert(ufs_read_command(m,1025,0,0,0,0,0,0)==-EINVAL);
    assert(ufs_read_command(m,UINT64_MAX-1023,0,0,0,0,0,0)==-EINVAL);
    for(unsigned op=1;op<256;op++)if(op!=0x12&&op!=0x88&&op!=0x9e)
      assert(ufs_read_command(m,0x1000,0,op,0,1,512,0)==-EPERM);
    assert(ufs_read_command(m,0x1000,0,0x88,0,129,512,0)==-EINVAL);
    assert(ufs_read_command(m,0x1000,0,0x88,UINT64_MAX,1,512,0)==-EINVAL);
    assert(ufs_read_command(m,0x1000,0,0x88,0x123456789abcdef0ULL,16,4096,0)==65536);
    assert(ufs_getle32(m+16)==0x1400&&ufs_getle32(m+24)==0x00800080);
    assert(ufs_getle32(m+28)==0x01000001);
    assert(ufs_getbe64(m+1024+18)==0x123456789abcdef0ULL);
    assert(ufs_getbe32(m+1024+26)==16);
    assert(ufs_getle32(m+2048)==0x2000&&ufs_getle32(m+2060)==65535);
    uint64_t blocks;uint32_t size;uint8_t d[32]={0};
    memset(d,0xff,8);ufs_be32(d+8,4096);
    assert(ufs_read_capacity(d,&blocks,&size)==-EOVERFLOW);
    assert(ufs_read_range(UINT64_MAX,2,1)==-EINVAL);
    free(m);
  }else if(scenario==12){
    for(highalloc=1;highalloc<=2;highalloc++){
      assert(ufs_attach(0)==-EADDRNOTAVAIL&&!g_ufs.dma&&!g_ufs.owns);
      assert(!writes&&!commands&&!block_registered&&freed==highalloc);
    }
  }else if(scenario==13){
    host_test_permit=false;
    assert(ufs_diag_write(&f,"board\n",6)==6&&g_ufs_board.stable);
    assert(g_ufs_board.reads==26&&!hci_reads&&!writes);
    assert(ufs_diag_write(&f,"probe\n",6)==-EACCES);
    assert(ufs_diag_write(&f,"attach-ro=0\n",12)==-EACCES);
    assert(!hci_reads&&!writes&&!commands&&!block_registered&&!g_ufs.dma);
    assert(ufs_diag_write(&f,"force\n",6)==-EINVAL);
    assert(ufs_diag_write(&f,"board=1\n",8)==-EINVAL);
    f.f_pos=0;assert(ufs_diag_read(&f,report,sizeof(report))>0);
    assert(strstr(report,"board-v166")&&strstr(report,"hci-access=blocked"));
    assert(!hci_reads&&!writes);
    assert(ufs_diag_write(&f,"\n",1)==1&&g_ufs.last==-EINVAL);
  }else if(scenario==14){
    host_test_permit=false;handoff_profile=true;
    regs[0x24/4]=0x30000;regs[0x60/4]=1;regs[0x80/4]=1;
    assert(ufs_diag_write(&f,"handoff",7)==7);
    assert(g_ufs_handoff.stable&&g_ufs_handoff.reads==32&&hci_reads==32);
    assert(g_ufs_handoff.link_ready&&g_ufs_handoff.needs_quiesce);
    assert(!g_ufs.last&&!g_ufs.dma&&!writes&&!commands&&!block_registered);
    assert(ufs_diag_write(&f,"\n",1)==1&&!g_ufs.last&&hci_reads==32);
    assert(ufs_diag_write(&f,"\r\n",2)==2&&!g_ufs.last&&hci_reads==32);
    assert(ufs_diag_write(&f,"",0)==0&&!g_ufs.last);
    assert(ufs_diag_write(&f,"attach-ro=0",11)==-EACCES);
    assert(ufs_diag_write(&f,"\n",1)==1&&g_ufs.last==-EACCES);
    assert(!writes&&!commands&&!g_ufs.dma&&!block_registered);
    f.f_pos=0;char whole[8192];ssize_t n=ufs_diag_read(&f,whole,sizeof(whole)-1);
    assert(n>0);whole[n]=0;assert(strstr(whole,"handoff-v167"));
    assert(strstr(whole,"attach-blocked=1")&&strstr(whole,"/dev/ufs0 STILL readonly"));
  }else if(scenario==15){
    host_test_permit=false;
    assert(ufs_diag_write(&f,"handoff",7)==-EXDEV&&!hci_reads);
    handoff_profile=true;drop_handoff_clocks=true;
    assert(ufs_diag_write(&f,"handoff",7)==-EAGAIN&&hci_reads==1);
    assert(!writes&&!commands&&!g_ufs.dma&&!block_registered);
  }else if(scenario==16){
    host_test_permit=false;
    assert(ufs_diag_write(&f,"quiesce",7)==-ENOTSUP&&!hci_reads&&!writes);
    assert(ufs_diag_write(&f,"dma-nop",7)==-ENOTSUP&&!hci_reads&&!writes);
    assert(ufs_diag_write(&f,"readcheck",9)==-ENOTSUP&&!hci_reads&&!writes);
    assert(ufs_diag_write(&f,"handoff",7)==-ENOTSUP&&!hci_reads);
    assert(ufs_diag_write(&f,"\n",1)==1&&g_ufs.last==-ENOTSUP);
  }else if(scenario==17){
    host_test_permit=false;handoff_profile=true;
    regs[0]=0x11870f1f;regs[0x30/4]=0x10f;
    regs[0x24/4]=0x30e15;regs[0x60/4]=1;regs[0x80/4]=1;
    regs[0x50/4]=0xbfeb4c00;
    assert(ufs_diag_write(&f,"quiesce",7)==7);
    assert(g_ufs_quiesce.completed&&writes==3&&!g_ufs.poisoned);
    assert(!regs[0x24/4]&&!regs[0x60/4]&&!regs[0x80/4]);
    assert(regs[0x50/4]==0xbfeb4c00&&!commands&&!g_ufs.dma&&!block_registered);
    assert(ufs_diag_write(&f,"\n",1)==1&&writes==3&&!g_ufs.last);
    assert(ufs_diag_write(&f,"quiesce",7)==-EALREADY&&writes==3);
    assert(ufs_diag_write(&f,"attach-ro=0",11)==-EACCES&&writes==3);
    assert(ufs_diag_write(&f,"handoff",7)==7&&!g_ufs_handoff.needs_quiesce);
    f.f_pos=0;char whole[8192];ssize_t n=ufs_diag_read(&f,whole,sizeof(whole)-1);
    assert(n>0);whole[n]=0;assert(strstr(whole,"quiesce-v168"));
    assert(strstr(whole,"reboot to retry")&&strlen(whole)<sizeof(whole)-1);
  }else if(scenario==18){
    host_test_permit=false;handoff_profile=true;drop_quiesce_clocks=true;
    regs[0]=0x11870f1f;regs[0x30/4]=0x10f;regs[0x24/4]=0x30e15;
    regs[0x60/4]=1;regs[0x80/4]=1;regs[0x50/4]=0xbfeb4c00;
    assert(ufs_diag_write(&f,"quiesce",7)==-EAGAIN);
    assert(writes==1&&g_ufs.poisoned&&g_ufs_quiesce.quarantine);
    assert(!commands&&!g_ufs.dma&&!block_registered);
    unsigned previous=hci_reads;
    assert(ufs_diag_write(&f,"quiesce",7)==-EBUSY&&writes==1&&hci_reads==previous);
    assert(ufs_diag_write(&f,"\n",1)==1&&g_ufs.last==-EBUSY);
  }else if(scenario>=19&&scenario<=22){
    host_test_permit=false;handoff_profile=true;noptrial=true;
    regs[0]=0x11870f1f;regs[0x30/4]=0x10f;regs[0x24/4]=0x30e15;
    regs[0x60/4]=1;regs[0x80/4]=1;regs[0x50/4]=0xbfeb4c00;
    if(scenario==20){
      assert(ufs_diag_write(&f,"dma-nop",7)==-EACCES&&!hci_reads&&!writes);
    }else{
      assert(ufs_diag_write(&f,"quiesce",7)==7&&writes==3);
      nop_map_fault=scenario==21;drop_nop_clocks=scenario==22;
      int ret=ufs_diag_write(&f,"dma-nop",7);
      if(scenario==19){
        assert(ret==7&&g_ufs_nop.completed&&g_ufs_nop.stopped&&g_ufs_nop.guards);
        assert(g_ufs_nop.pages==19&&g_ufs_nop.writes==5&&writes==8&&commands==1);
        assert(cleaned==1&&invalidated==1&&!g_ufs.poisoned);
        assert(regs[0x50/4]==g_ufs_nop.physical&&!regs[0x60/4]&&regs[8]==1);
        unsigned old=hci_reads;assert(ufs_diag_write(&f,"dma-nop",7)==-EALREADY&&hci_reads==old);
        assert(ufs_diag_write(&f,"attach-ro=0",11)==-EACCES&&writes==8);
        f.f_pos=0;char whole[8192];ssize_t n=ufs_diag_read(&f,whole,sizeof(whole)-1);
        assert(n>0);whole[n]=0;assert(strstr(whole,"DMA routing is validated"));
      }else if(scenario==21){
        assert(ret==-EFAULT&&writes==3&&!commands&&!cleaned&&!g_ufs.poisoned);
      }else{
        assert(ret==-EAGAIN&&writes==4&&!commands&&g_ufs.poisoned&&g_ufs_nop.quarantine);
        assert(ufs_diag_write(&f,"dma-nop",7)==-EBUSY&&writes==4);
      }
    }
    assert(!g_ufs.dma&&!g_ufs.owns&&!block_registered&&!freed);
  }else if(scenario>=23&&scenario<=25){
    host_test_permit=false;handoff_profile=true;noptrial=true;
    regs[0]=0x11870f1f;regs[0x30/4]=0x10f;regs[0x24/4]=0x30e15;
    regs[0x60/4]=1;regs[0x80/4]=1;regs[0x50/4]=0xbfeb4c00;
    assert(ufs_diag_write(&f,"readcheck=8",11)==-EINVAL&&!writes&&!hci_reads);
    assert(ufs_diag_write(&f,"readcheck",9)==-EACCES&&!writes&&!hci_reads);
    if(scenario!=23){
      memset(&g_ufs_readcheck,0,sizeof(g_ufs_readcheck)); /* HOST scenario reset ONLY */
      assert(ufs_diag_write(&f,"quiesce",7)==7);
      assert(ufs_diag_write(&f,"dma-nop",7)==7);
      drop_read_clocks=scenario==25;
      int ret=ufs_diag_write(&f,"readcheck=2",11);
      if(scenario==24){
        assert(ret==11&&g_ufs_readcheck.completed&&g_ufs_readcheck.samples==3);
        assert(g_ufs_readcheck.lun==2&&g_ufs_readcheck.stopped&&g_ufs_readcheck.guards);
        assert(g_ufs_readcheck.commands==8&&g_ufs_readcheck.writes==34&&!g_ufs_readcheck.quarantine);
        unsigned old=hci_reads;
        assert(ufs_diag_write(&f,"readcheck",9)==-EALREADY&&hci_reads==old);
        assert(ufs_diag_write(&f,"attach-ro=0",11)==-EACCES);
        f.f_pos=0;char whole[8192];ssize_t n=ufs_diag_read(&f,whole,sizeof(whole)-1);
        assert(n>0);whole[n]=0;assert(strstr(whole,"readcheck-v170")&&strlen(whole)<sizeof(whole)-1);
      }else{
        assert(ret==-EAGAIN&&g_ufs_readcheck.quarantine&&g_ufs.poisoned&&commands==1);
        assert(ufs_diag_write(&f,"readcheck",9)==-EBUSY);
      }
    }
    assert(!g_ufs.dma&&!block_registered&&!freed);
#ifdef CONFIG_A733_UFS_BLOCK_RO
  }else if(scenario>=26&&scenario<=37){
    host_test_permit=false;handoff_profile=true;noptrial=true;
    regs[0]=0x11870f1f;regs[0x30/4]=0x10f;regs[0x24/4]=0x30e15;
    regs[0x60/4]=1;regs[0x80/4]=1;regs[0x50/4]=0xbfeb4c00;
    assert(ufs_diag_write(&f,"block-ro",8)==-EACCES&&!writes&&!hci_reads);
    assert(ufs_diag_write(&f,"bench-ro",8)==-ENODEV&&!writes&&!hci_reads);
    assert(ufs_diag_write(&f,"quiesce",7)==7);
    assert(ufs_diag_write(&f,"dma-nop",7)==7);
    assert(ufs_diag_write(&f,"readcheck",9)==9);
    if(scenario==30){block_register_error=-ENOMEM;assert(ufs_diag_write(&f,"block-ro",8)==-ENOMEM);
      assert(!g_ufs.registered&&!g_ufs_live_ro&&!block_registered&&!g_ufs.poisoned);}
    else{
      unsigned before=writes;assert(ufs_diag_write(&f,"block-ro",8)==8&&writes==before);
      assert(g_ufs.registered&&g_ufs_live_ro&&block_registered==1&&!g_ufs.dma&&!g_ufs.owns);
      assert(ufs_diag_write(&f,"block-ro",8)==-EALREADY);
      struct geometry geo;assert(!published_ops->geometry(NULL,&geo));
      assert(geo.geo_available&&!geo.geo_writeenabled&&geo.geo_sectorsize==4096&&geo.geo_nsectors==33554432);
      static unsigned char buffer[1048576];
      if(scenario==26){
        assert(published_ops->read(NULL,buffer,27,256)==256);
        for(unsigned i=0;i<sizeof(buffer);i++)assert(buffer[i]==0x7b);
        before=writes;assert(published_ops->read(NULL,buffer,-1,1)==-EINVAL&&writes==before);
        assert(published_ops->read(NULL,buffer,0,257)==-E2BIG&&writes==before);
        assert(published_ops->read(NULL,NULL,33554432,0)==0&&writes==before);
        assert(ufs_diag_write(&f,"attach-ro=0",11)==-EBUSY);
      }else if(scenario==27){
        assert(ufs_diag_write(&f,"bench-ro=0",10)==-EINVAL);
        assert(ufs_diag_write(&f,"bench-ro=17",11)==-EINVAL);
        assert(ufs_diag_write(&f,"bench-ro=x",10)==-EINVAL);
        assert(ufs_diag_write(&f,"bench-ro=1",10)==10);
        assert(g_ufs_benchmark.completed&&g_ufs_benchmark.passes==2&&g_ufs_benchmark.bytes==2097152);
        assert(g_ufs_benchmark.crc[0]==g_ufs_benchmark.crc[1]);
        assert(ufs_diag_write(&f,"bench-ro",8)==-EALREADY);
        f.f_pos=0;char full[8192];ssize_t n=ufs_diag_read(&f,full,sizeof(full)-1);
        assert(n>0&&n<(ssize_t)sizeof(full)-1);full[n]=0;
        assert(strstr(full,"bench-ro-v171")&&strstr(full,"block-v171: published=1"));
      }else if(scenario==28){
        fault=1;assert(published_ops->read(NULL,buffer,0,1)==-ETIMEDOUT&&g_ufs.poisoned);
        before=writes;assert(published_ops->read(NULL,buffer,0,1)==-ENODEV&&writes==before);
      }else if(scenario==29){
        clocks=false;assert(published_ops->read(NULL,buffer,0,1)==-EAGAIN&&g_ufs.poisoned);
      }else if(scenario==31||scenario==32){
        bulk_corrupt=scenario==31;bulk_repeat_corrupt=scenario==32;
        assert(ufs_diag_write(&f,"bench-ro=1",10)==-EBADMSG&&g_ufs.poisoned);
        assert(!g_ufs_benchmark.completed&&g_ufs_benchmark.error==-EBADMSG);
        if(scenario==31)assert(g_ufs_benchmark.passes==0);
        else assert(g_ufs_benchmark.passes==2&&g_ufs_benchmark.crc[0]!=g_ufs_benchmark.crc[1]);
        before=writes;assert(published_ops->read(NULL,buffer,0,1)==-ENODEV&&writes==before);
      }else if(scenario==33){
        before=writes;busy=true;
        assert(published_ops->read(NULL,buffer,0,1)==-EBUSY&&writes==before);
        assert(ufs_diag_write(&f,"bench-ro=1",10)==-EBUSY&&writes==before);
        busy=false;assert(published_ops->read(NULL,buffer,0,1)==1);
      }else if(scenario==34){
        unsigned old=commands;
        for(unsigned i=0;i<100;i++)assert(published_ops->read(NULL,buffer,i,1)==1);
        assert(commands==old+100&&g_ufs_readcheck.reader_calls==100&&!freed);
        assert(ufs_diag_write(&f,"quiesce",7)==-EBUSY);
      }else if(scenario==37){
#ifdef CONFIG_A733_UFS_DATA
        unsigned old=commands;
        assert(ufs_diag_write(&f,"data-ro",7)==-EACCES&&commands==old);
        /* Host-only identity fixture; no actual disk or scratch writes. */
        struct a733_ufs_readcheck *r=&g_ufs_readcheck;
        strcpy(r->model,"KINGSTONCY14-128        0002");r->blocks=31256576;
        r->blocksize=4096;r->lun=0;r->protective_mbr=true;
        r->gpt_header_valid=r->gpt_entries_valid=true;r->gpt_error=0;
        r->gpt_partitions=3;r->entry_bytes=16384;r->entry_lba=2;
        const uint64_t parts[3][2]={{32768,65535},{65536,679935},{679936,31256542}};
        memcpy(r->partitions,parts,sizeof(parts));
        assert(ufs_diag_write(&f,"data-ro",7)==7&&g_ufs_data_ro_registered);
        assert(commands==old&&!g_ufs_scratch.prepared&&!g_ufs_data.enabled);
        assert(!published_ops->write&&!published_ops->ioctl);
        assert(!published_ops->geometry(NULL,&geo)&&geo.geo_available&&!geo.geo_writeenabled);
        assert(geo.geo_nsectors==A733_UFS_DATA_BLOCKS);
        assert(published_ops->read(NULL,buffer,0,1)==1);
        old=commands;
        assert(ufs_diag_write(&f,"data-ro",7)==-EALREADY);
        const char *ack="data-rw=ERASE-TEST-LINUX";
        assert(ufs_diag_write(&f,ack,strlen(ack))==-EBUSY&&commands==old);
#endif
      }else if(scenario==36){
#ifdef CONFIG_A733_UFS_DATA
        unsigned old=commands;struct geometry geo;
        assert(ufs_diag_write(&f,"data-rw=ERASE-TEST-LINUX",23)==-EINVAL);
        const char *ack="data-rw=ERASE-TEST-LINUX";
        assert(ufs_diag_write(&f,ack,strlen(ack))==-EACCES&&commands==old);
        assert(ufs_diag_write(&f,"data-rw",7)==-EINVAL&&commands==old);
        assert(ufs_data_ioctl(NULL,0,0)==-ENOTTY&&commands==old);
        assert(ufs_data_ioctl(NULL,BIOC_FLUSH,1)==-ENOTTY&&commands==old);
        assert(ufs_data_ioctl(NULL,BIOC_FLUSH,0)==-ENODEV&&commands==old);
        assert(ufs_data_read(NULL,buffer,0,1)==-ENODEV);
        assert(ufs_data_write(NULL,buffer,0,1)==-ENODEV);
        assert(!ufs_data_geometry(NULL,&geo)&&!geo.geo_available&&!geo.geo_writeenabled);
        assert(geo.geo_nsectors==A733_UFS_DATA_BLOCKS&&geo.geo_sectorsize==4096);
        assert(!g_ufs_block_ops.write&&!g_ufs_block_ops.ioctl&&commands==old);
#endif
      }else if(scenario==35){
#ifdef CONFIG_A733_UFS_SCRATCH
        unsigned old=commands;char backup[32];struct file bf={0};
        assert(ufs_backup_read(&bf,backup,sizeof(backup))==-ENODATA);
        assert(ufs_diag_write(&f,"scratch-prepare",15)==-EACCES&&commands==old);
        assert(ufs_diag_write(&f,"scratch-test=ERASE-TEST-LINUX",29)==-EACCES&&commands==old);
        assert(ufs_diag_write(&f,"scratch-test",12)==-EINVAL&&commands==old);
        assert(ufs_diag_write(&f,"bench-ro=8",10)==10&&g_ufs_benchmark.completed);
        old=commands;assert(ufs_diag_write(&f,"scratch-prepare",15)==-EACCES&&commands==old);
        memset(g_ufs_scratch.original,0x5c,65536);g_ufs_scratch.prepared=true;
        g_ufs_scratch.quarantine=true;g_ufs.poisoned=true;
        assert(ufs_backup_read(&bf,backup,sizeof(backup))==32&&backup[0]==0x5c);
        bf.f_pos=65530;assert(ufs_backup_read(&bf,backup,32)==6);
        assert(ufs_backup_read(&bf,backup,32)==0);
        assert(ufs_diag_write(&f,"scratch-prepare",15)==-EIO&&commands==old);
#endif
      }
    }
    assert(!freed);
#endif
  }else{assert(!"unknown scenario");}
  if(g_ufs.dma)assert(!munmap(g_ufs.dma,A733_UFS_DMA_SIZE)); /* Host teardown ONLY */
  printf("UFS mock scenario %d PASS (not hardware)\n",scenario);return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='a733-ufs-') as tmp:
    tmp = Path(tmp)
    source = '\n'.join(line for line in (CHIP/'a733_ufs.c').read_text().splitlines()
                       if not line.startswith('#include'))
    (tmp/'test.c').write_text(PREFIX + source + TEST)
    exe = tmp/'test'
    base = ['gcc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fno-pie', '-no-pie', '-fsanitize=address,undefined',
                    '-I'+str(CHIP), str(tmp/'test.c'), str(CHIP/'a733_ufs_board.c'),
                    str(CHIP/'a733_ufs_handoff.c'), str(CHIP/'a733_ufs_quiesce.c'), str(CHIP/'a733_ufs_nop.c'), str(CHIP/'a733_ufs_readcheck.c'), str(CHIP/'a733_ufs_scratch.c'), str(CHIP/'a733_ufs_data.c'), '-o', str(exe)]
    subprocess.run(base+['-DCONFIG_A733_UFS_HANDOFF_PROBE=1'], check=True)
    for scenario in range(16):
        subprocess.run([str(exe), str(scenario)], check=True,
                       env={**os.environ, 'ASAN_OPTIONS':'detect_leaks=1'})
    subprocess.run(base,check=True)
    subprocess.run([str(exe),'16'],check=True,
        env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1'})
    subprocess.run(base+['-DCONFIG_A733_UFS_HANDOFF_PROBE=1','-DCONFIG_A733_UFS_QUIESCE=1'],check=True)
    subprocess.run([str(exe),'17'],check=True,
        env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1'})
    subprocess.run([str(exe),'18'],check=True,
        env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1'})
    subprocess.run(base+['-DCONFIG_A733_UFS_HANDOFF_PROBE=1','-DCONFIG_A733_UFS_QUIESCE=1',
        '-DCONFIG_A733_UFS_DMA_NOP=1','-Wl,-Ttext-segment=0x40200000'],check=True)
    for scenario in range(19,23):
        subprocess.run([str(exe),str(scenario)],check=True,
            env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1'})
    subprocess.run(base+['-DCONFIG_A733_UFS_HANDOFF_PROBE=1','-DCONFIG_A733_UFS_QUIESCE=1',
        '-DCONFIG_A733_UFS_DMA_NOP=1','-DCONFIG_A733_UFS_READCHECK=1',
        '-Wl,-Ttext-segment=0x40200000'],check=True)
    for scenario in range(23,26):
        subprocess.run([str(exe),str(scenario)],check=True,
            env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1'})
    subprocess.run(base+['-DCONFIG_A733_UFS_HANDOFF_PROBE=1','-DCONFIG_A733_UFS_QUIESCE=1',
        '-DCONFIG_A733_UFS_DMA_NOP=1','-DCONFIG_A733_UFS_READCHECK=1','-DCONFIG_A733_UFS_BLOCK_RO=1',
        '-Wl,-Ttext-segment=0x40200000'],check=True)
    for scenario in range(26,35):
        subprocess.run([str(exe),str(scenario)],check=True,
            env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1'})
    subprocess.run(base+['-DCONFIG_A733_UFS_HANDOFF_PROBE=1','-DCONFIG_A733_UFS_QUIESCE=1',
        '-DCONFIG_A733_UFS_DMA_NOP=1','-DCONFIG_A733_UFS_READCHECK=1','-DCONFIG_A733_UFS_BLOCK_RO=1',
        '-DCONFIG_A733_UFS_SCRATCH=1','-Wl,-Ttext-segment=0x40200000'],check=True)
    subprocess.run([str(exe),'35'],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1'})
    subprocess.run(base+['-DCONFIG_A733_UFS_HANDOFF_PROBE=1','-DCONFIG_A733_UFS_QUIESCE=1',
        '-DCONFIG_A733_UFS_DMA_NOP=1','-DCONFIG_A733_UFS_READCHECK=1','-DCONFIG_A733_UFS_BLOCK_RO=1',
        '-DCONFIG_A733_UFS_SCRATCH=1','-DCONFIG_A733_UFS_DATA=1',
        '-Wl,-Ttext-segment=0x40200000'],check=True)
    subprocess.run([str(exe),'36'],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1'})
    subprocess.run([str(exe),'37'],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1'})
