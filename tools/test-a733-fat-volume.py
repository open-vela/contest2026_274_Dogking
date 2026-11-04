#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Official FAT32 planner/formatter with actual 4KiB UFS data geometry.
Only a new /tmp sparse REGULAR file is written. No block device or SDK edits.
"""
from pathlib import Path
import argparse,hashlib,os,subprocess,tempfile
p=argparse.ArgumentParser(description=__doc__);p.add_argument('sdk',type=Path);a=p.parse_args()
sdk=a.sdk.resolve();fat=sdk/'apps/fsutils/mkfatfs'
config=r'''
#define FAR
#define OK 0
#define ERROR (-1)
#define UNUSED(x) ((void)(x))
#define CONFIG_DEBUG_FEATURES 1
'''
fixture=r'''
#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "fsutils/mkfatfs.h"
#include "mkfatfs.h"
#include "a733_ufs_volume.h"
static uint16_t le16(const uint8_t *p){return p[0]|(uint16_t)p[1]<<8;}
static uint32_t le32(const uint8_t *p){return le16(p)|(uint32_t)le16(p+2)<<16;}
int main(int argc,char **argv)
{
  assert(argc==2);struct fat_format_s fmt=FAT_FORMAT_INITIALIZER;
  struct fat_var_s var={0};fmt.ff_fattype=32;fmt.ff_nsectors=30576479;
  var.fv_fattype=32;var.fv_sectorsize=4096;var.fv_sectshift=12;
  assert(!mkfatfs_configfatfs(&fmt,&var)&&fmt.ff_fattype==32);
  assert(var.fv_nclusters>=65524&&var.fv_nclusters<0x0ffffff0);
  assert((uint64_t)var.fv_nfatsects*4096>=(uint64_t)(var.fv_nclusters+2)*4);
  uint64_t end=fmt.ff_rsvdseccount+(uint64_t)fmt.ff_nfats*var.fv_nfatsects+
                ((uint64_t)var.fv_nclusters<<fmt.ff_clustshift);
  assert(end<=30576479);
  var.fv_fd=open(argv[1],O_CREAT|O_EXCL|O_RDWR,0600);assert(var.fv_fd>=0);
  struct stat st;assert(!fstat(var.fv_fd,&st)&&S_ISREG(st.st_mode));
  assert(!ftruncate(var.fv_fd,UINT64_C(30576479)*4096));
  var.fv_sect=malloc(4096);assert(var.fv_sect);
  assert(!mkfatfs_writefatfs(&fmt,&var));
  uint8_t boot[4096],backup[4096],info[4096];
  assert(pread(var.fv_fd,boot,4096,0)==4096);
  assert(le16(boot+11)==4096&&boot[13]==(1u<<fmt.ff_clustshift));
  assert(le32(boot+32)==30576479&&le32(boot+36)==var.fv_nfatsects);
  assert(boot[510]==0x55&&boot[511]==0xaa);
  assert(pread(var.fv_fd,backup,4096,(off_t)fmt.ff_backupboot*4096)==4096);
  assert(!memcmp(boot,backup,4096));
  assert(pread(var.fv_fd,info,4096,4096)==4096&&le32(info)==0x41615252);
  assert(le32(info+484)==0x61417272);
  unsigned fsinfo=0,back=0;
  assert(!ufs_volume_boot(boot,30576479,&fsinfo,&back));
  assert(fsinfo==1&&back==fmt.ff_backupboot&&!ufs_volume_info(info));
  unsigned fields[]={11,13,14,16,17,19,22,32,36,40,42,44,48,50,510,511};
  for(unsigned j=0;j<sizeof(fields)/sizeof(fields[0]);j++)
    {
      uint8_t damaged[4096];memcpy(damaged,boot,4096);
      unsigned pos=fields[j];damaged[pos]=pos==13?3:pos==36?0:0xff;
      if(pos==14){damaged[14]=damaged[15]=0;}
      if(pos==44)memset(damaged+44,0xff,4);
      assert(ufs_volume_boot(damaged,30576479,&fsinfo,&back)<0);
    }
  info[484]^=1;assert(ufs_volume_info(info)<0);
  for(unsigned i=0;i<fmt.ff_nfats;i++)
    {
      off_t pos=((off_t)fmt.ff_rsvdseccount+i*var.fv_nfatsects)*4096;
      assert(pread(var.fv_fd,info,4096,pos)==4096);
      assert((le32(info+8)&0x0fffffff)>=0x0ffffff8);
    }
  assert(!fsync(var.fv_fd)&&!close(var.fv_fd));free(var.fv_sect);
  printf("official FAT32: sectors=30576479 bytes=%" PRIu64 " cluster-sectors=%u FAT-sectors=%u metadata/backup/FAT PASS; no hardware mount\n",
    UINT64_C(30576479)*4096,1u<<fmt.ff_clustshift,var.fv_nfatsects);
  return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='a733-fat-') as folder:
    tmp=Path(folder)
    for name,body in {
        'nuttx/config.h':config,
        'nuttx/kmalloc.h':'#include <stdlib.h>\n',
        'nuttx/fs/fat.h':(sdk/'nuttx/include/nuttx/fs/fat.h').read_text(),
        'debug.h':'#define finfo(...) ((void)0)\n#define ferr(...) ((void)0)\n#define fwarn(...) ((void)0)\n',
        'fsutils/mkfatfs.h':(sdk/'apps/include/fsutils/mkfatfs.h').read_text(),
        'fixture.c':fixture}.items():
        target=tmp/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_text(body)
    exe=tmp/'test'
    subprocess.run(['gcc','-D_GNU_SOURCE','-std=c11','-O1','-g','-fno-pie','-no-pie',
        '-fsanitize=address,undefined','-I'+str(tmp),'-I'+str(fat),
        '-I'+str(Path(__file__).resolve().parent.parent/'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733'),
        '-include','string.h',str(fat/'configfat.c'),str(fat/'writefat.c'),
        str(tmp/'fixture.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe),str(tmp/'volume.img')],check=True,timeout=30,
                   env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1'})
    for name in ['configfat.c','writefat.c','fat32.h','mkfatfs.h']:
        print(name+' SHA256='+hashlib.sha256((fat/name).read_bytes()).hexdigest())
