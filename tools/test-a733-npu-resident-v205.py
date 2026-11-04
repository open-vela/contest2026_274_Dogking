#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Actual resident import/lifecycle/load helpers; simulated hardware runner only."""
from pathlib import Path
import os,re,subprocess,tempfile
r=Path(__file__).resolve().parents[1];chip=r/'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733'
runtime=(chip/'a733_npu_runtime.c').read_text()
definitions='\n'.join(x for x in runtime.splitlines() if x.startswith('#define A733_RT_A7PM_') and 'POOL_SIZE' not in x)
for name in ('header','region'):
    definitions+='\n'+re.search(r'struct a733_rt_a7pm_'+name+r'_s\s*\{.*?\}\s*__attribute__\(\(packed\)\);',runtime,re.S)[0]
code=r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <nuttx/npu/a733_vip2.h>
#include <system/a733_vision.h>
#define kmm_malloc malloc
#define kmm_free free
#define kmm_zalloc(n) calloc(1,n)
#define strlcpy mock_strlcpy
static size_t mock_strlcpy(char *d,const char *s,size_t n){size_t l=strlen(s);snprintf(d,n,"%s",s);return l;}
static uint32_t crc32part(const uint8_t *p,size_t n,uint32_t seed){return a733_vision_crc32(p,n,seed);}
struct file{FILE *f;};
struct a733_rt_live_s;
struct a733_rt_context_s{struct a733_rt_live_s *live;};
struct a733_npu_buffer_s{void *cpu;size_t size;};
static const char *model;
static unsigned opens,reads,invalidates,executions;
static bool expected_warm,fail_hardware;
static int a733_rt_validate_path(const char *s){return memchr(s,0,A733_NPU_MODEL_PATH_MAX)?0:-EINVAL;}
static int file_open(struct file *f,const char *p,int flags){assert(!strcmp(p,"/camram-v204/model.a7pm") && flags==(O_RDONLY|O_CLOEXEC));++opens;f->f=fopen(model,"rb");return f->f?0:-errno;}
static off_t file_seek(struct file *f,off_t pos,int whence){return fseeko(f->f,pos,whence)?-errno:ftello(f->f);}
static int file_close(struct file *f){return fclose(f->f)?-errno:0;}
static int a733_rt_file_read_exact(struct file *f,off_t off,void *dst,size_t n,uint32_t *crc)
{++reads;if(file_seek(f,off,SEEK_SET)<0)return -EIO;if(fread(dst,1,n,f->f)!=n)return -EIO;if(crc)*crc=crc32part(dst,n,*crc);return 0;}
static void a733_npu_mem_invalidate(struct a733_npu_buffer_s *b){assert(b && b->cpu);++invalidates;}
'''
code+=definitions+'\n#include "'+str(chip/'a733_npu_live.inc')+'"\n'
code+=r'''
static int a733_rt_run_a7pm(struct a733_npu_run_s *p)
{assert(g_rt_live && g_rt_live->warm==expected_warm);++executions;p->hardware_status=fail_hardware?-EIO:0;return fail_hardware?-EIO:0;}
int main(int argc,char **argv)
{
  assert(argc==2);model=argv[1];struct a733_rt_context_s c={0};
  struct a733_npu_live_prepare_s p={.version=A733_NPU_LIVE_ABI};
  strcpy(p.path,"/data/wrong.a7pm");assert(a733_rt_live_import(&c,&p)==-EACCES && !opens);
  strcpy(p.path,"/camram-v204/model.a7pm");p.version=99;
  assert(a733_rt_live_import(&c,&p)==-EINVAL && !opens);p.version=A733_NPU_LIVE_ABI;
  assert(!a733_rt_live_import(&c,&p) && opens==1 && reads==1 && c.live);
  assert(p.bytes==8017024 && p.crc32==0xf5f558c5);
  assert(a733_rt_live_import(&c,&p)==-EALREADY && opens==1);
  struct a733_npu_run_s run={.flags=A733_NPU_RUN_INPUT_FILE};strcpy(run.package_path,p.path);
  expected_warm=false;assert(!a733_rt_live_run(&c,&run) && c.live->loaded);
  expected_warm=true;assert(!a733_rt_live_run(&c,&run) && opens==1 && reads==1);
  g_rt_live=c.live;
  uint8_t copy[256];struct a733_npu_buffer_s b={copy,sizeof(copy)};
  struct a733_rt_a7pm_region_s region={.file_offset=128,.size=sizeof(copy),.flags=A733_RT_A7PM_LOAD};
  region.crc32=crc32part(c.live->snapshot+128,sizeof(copy),0);uint32_t crc=0;
  /* v208 exact full-region dynamic replacement, never partial/padded LOADs.
   * The immutable CPU template stays checked, not silently trusted. */
  assert(!a733_rt_live_skip_input(0,&region,sizeof(copy)-1,region.crc32,true));
  assert(!a733_rt_live_skip_input(0,&region,sizeof(copy),region.crc32,false));
  assert(a733_rt_live_skip_input(0,&region,sizeof(copy),region.crc32^1,true)==-EBADMSG);
  assert(a733_rt_live_skip_input(0,&region,sizeof(copy),region.crc32,true)==1);
  assert(c.live->input_skipped[0] && c.live->input_template_ok[0]);
  assert(a733_rt_live_skip_input(0,&region,sizeof(copy),region.crc32,true)==1);
  region.crc32^=1;
  assert(a733_rt_live_skip_input(0,&region,sizeof(copy),region.crc32^1,true)==-EBADMSG);
  region.crc32^=1;region.flags|=A733_RT_A7PM_COMMAND;
  assert(!a733_rt_live_skip_input(0,&region,sizeof(copy),region.crc32,true));
  region.flags=A733_RT_A7PM_LOAD;
  c.live->warm=false;assert(!a733_rt_live_load(NULL,0,&region,&b,false,&crc) && crc==region.crc32);
  c.live->warm=true;c.live->eligible[0]=true;crc=0;
  assert(!a733_rt_live_load(NULL,0,&region,&b,false,&crc) && invalidates==0 && c.live->profile.retained_regions==1);
  assert(a733_rt_live_verify_load(0,&region,&b) && invalidates==1);
  assert(!a733_rt_live_clean_required(0,&region));
  copy[0]^=1;crc=0;
  /* A leased region avoids a DUPLICATE pre-run scan. Corruption still MUST
   * reject the same execution at post-run verification before publication. */
  assert(!a733_rt_live_load(NULL,0,&region,&b,false,&crc));
  assert(!a733_rt_live_verify_load(0,&region,&b) && !c.live->eligible[0]);
  c.live->eligible[0]=true;g_rt_live_owner=NULL;crc=0;
  assert(a733_rt_live_load(NULL,0,&region,&b,false,&crc)==-ESTALE);
  g_rt_live_owner=c.live;
  /* Every command/input reloads, even if an eligible bit were corrupted. */
  region.flags|=A733_RT_A7PM_COMMAND;crc=0;
  assert(!a733_rt_live_load(NULL,0,&region,&b,false,&crc) && crc==region.crc32);
  assert(a733_rt_live_clean_required(0,&region));
  copy[0]^=1;region.flags=A733_RT_A7PM_LOAD;crc=0;
  assert(!a733_rt_live_load(NULL,0,&region,&b,true,&crc) && crc==region.crc32);
  assert(a733_rt_live_clean_required(0,&region));
  region.flags|=A733_RT_A7PM_OUTPUT;assert(a733_rt_live_clean_required(0,&region));
  assert(a733_rt_live_read(NULL,8017023,copy,2,NULL)==-EFBIG);
  assert(a733_rt_live_read(NULL,-1,copy,1,NULL)==-EFBIG);
  g_rt_live=NULL;fail_hardware=true;expected_warm=true;
  assert(a733_rt_live_run(&c,&run)==-EIO && !c.live->loaded && !g_rt_live_owner);
  fail_hardware=false;expected_warm=false;assert(!a733_rt_live_run(&c,&run));
  g_rt_live_owner=NULL;expected_warm=false;assert(!a733_rt_live_run(&c,&run)); /* foreign pool mutation */
  strcpy(run.package_path,"/data/wrong.a7pm");unsigned before=executions;
  assert(a733_rt_live_run(&c,&run)==-EINVAL && executions==before);
  a733_rt_live_release(&c);assert(!c.live && !g_rt_live_owner);
  a733_rt_live_release(&c);
  puts("PASS actual resident import/raw CRC, immutable reads, warm region validation, command/input refresh, fault/foreign-owner invalidation, release; hardware runner MOCKED");
  return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='resident205-') as d:
    d=Path(d);source=d/'test.c';source.write_text(code);exe=d/'test';model=d/'model'
    host=(r/'../../quickly-openvela/prebuilts/build-tools/linux-x86_64/bin').resolve()
    alias=d/'mcopy';alias.symlink_to(host/'mtools')
    image=r.parent/'openvela-a733-cubie-a7z-sd-v204-camera-yolo-live-candidate.img'
    subprocess.run([str(alias),'-i',str(image)+'@@469762048','::/npu/yolov8n-pcq-v3.a7pm',str(model)],check=True,timeout=60,
      env={**os.environ,'PATH':str(host)+os.pathsep+os.environ.get('PATH','')})
    subprocess.run(['gcc','-std=c11','-D_DEFAULT_SOURCE','-O1','-g','-Wall','-Wextra','-Werror',
      '-fno-pie','-no-pie','-fsanitize=address,undefined','-I'+str(r/'tools/live-host-fixture'),
      '-I'+str(r/'board/a733-cubie-a7z/openvela-overlay/nuttx/include'),
      '-I'+str(r/'board/a733-cubie-a7z/openvela-overlay/apps/include'),str(source),'-o',str(exe)],check=True)
    subprocess.run([str(exe),str(model)],check=True,timeout=30)
