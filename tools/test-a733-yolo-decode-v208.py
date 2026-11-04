#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Actual v207 vs v208 decoder, bit-exact detection/candidate semantics."""
from pathlib import Path
import re,subprocess,tempfile
r=Path(__file__).resolve().parents[1]
path='board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733/a733_npu_runtime.c'
new=(r/path).read_text();old=subprocess.check_output(['git','show','3c0327e:'+path],cwd=r).decode()
start='static float a733_rt_yolo_sigmoid(';end='static int a733_rt_file_read_exact('
def extract(s):
    a=s.index(start);b=s.index(end,a);return s[a:b]
prefix='\n'.join(x for x in new.splitlines() if x.startswith('#define A733_RT_YOLO_'))
for tag in ('a733_rt_yolo_candidate_s','a733_rt_yolo_detection_s','a733_rt_a7pm_output_s'):
    prefix+='\n'+re.search(r'struct '+tag+r'\s*\{.*?\}(?:\s*__attribute__\(\(packed\)\))?;',new,re.S)[0]
body=r"""
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <errno.h>
#define OK 0
struct a733_npu_buffer_s { void *cpu; };
""" +prefix+r"""
static struct { unsigned yolo_candidates,yolo_detections;struct a733_rt_yolo_detection_s yolo[A733_RT_YOLO_MAX_DETECTIONS]; } current,reference;
"""
with tempfile.TemporaryDirectory(prefix='decode208-') as tmp:
    d=Path(tmp)
    # Keep all helper algorithms unchanged, rename two independent copies.
    for src,name in ((old,'old'),(new,'new')):
        code=extract(src)
        code=code.replace('g_rt.',('reference.' if name=='old' else 'current.'))
        code=re.sub(r'\ba733_rt_yolo_(\w+)(?=\s*\()',lambda m:'yolo_'+name+'_'+m[1],code)
        body+='\n'+code
    body+=r"""
int main(void) {
  unsigned sizes[6]={1638400,2048000,409600,512000,102400,128000};
  struct a733_npu_buffer_s mapped[6];struct a733_rt_a7pm_output_s outputs[6]={0};
  for(unsigned i=0;i<6;++i){mapped[i].cpu=malloc(sizes[i]);assert(mapped[i].cpu);outputs[i].region_index=i;outputs[i].size=sizes[i];}
  unsigned state=208;
  for(unsigned pass=0;pass<16;++pass) {
    for(unsigned i=0;i<6;++i)for(unsigned j=0;j<sizes[i]/4;++j) {
      state=state*1664525u+1013904223u;
      float v=(float)(int)(state>>20)/128.0f-16.0f;
      if(pass==0)v=0;
      if(pass==1)v=-INFINITY;
      if(pass==2)v=NAN;
      if(pass==3 && (j%97)==0)v=INFINITY;
      ((float*)mapped[i].cpu)[j]=v;
    }
    memset(&reference,0,sizeof(reference));memset(&current,0,sizeof(current));
    int a=yolo_old_decode(mapped,outputs,6),b=yolo_new_decode(mapped,outputs,6);
    assert(a==b && !memcmp(&reference,&current,sizeof(current)));
  }
  for(unsigned i=0;i<6;++i)free(mapped[i].cpu);
  puts("PASS v207/v208 actual tiled decoder exact struct bytes, zero/random/tie/NaN/infinity fixtures; no model/hardware execution");
}
"""
    (d/'test.c').write_text(body)
    subprocess.run(['gcc','-std=c11','-O2','-fno-fast-math','-Wall','-Wextra','-Werror',
      '-fsanitize=address,undefined','-fno-pie','-no-pie',
      str(d/'test.c'),'-lm','-o',str(d/'test')],check=True)
    subprocess.run([str(d/'test')],check=True,timeout=60)
