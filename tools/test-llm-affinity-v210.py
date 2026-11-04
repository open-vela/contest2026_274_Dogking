#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Execute the actual LLM affinity function with deterministic physical-core mocks."""
from pathlib import Path
import re,subprocess,tempfile
r=Path(__file__).resolve().parents[1]
name='board/a733-cubie-a7z/openvela-overlay/apps/system/aipetllm/aipetllm_modelcheck.c'
source=(r/name).read_text()
prior=subprocess.check_output(['git','show','a733-v209-affinity-async-sealed-20261029:'+name],cwd=r,text=True)
pattern=r'  for \(row = worker->first;.*?\n  return NULL;\n\}'
assert re.search(pattern,source,re.S)[0]==re.search(pattern,prior,re.S)[0],'numerical row loop changed'
function=re.search(r'static int llm_affinity\(int cpu\).*?\n\}',source,re.S)[0]
header=r/'board/a733-cubie-a7z/openvela-overlay/apps/include/system/a733_cpu_qualify.h'
test=r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "HEADER"
static uint32_t mask=255;
static unsigned actual=1,sets,waits;
static int stopped,stuck,badpart,geterror;
static uint64_t now;
static int llm_aff_get(void *p,uint32_t *out){(void)p;if(geterror)return -EIO;*out=mask;return 0;}
static int llm_aff_set(void *p,uint32_t m){(void)p;mask=m;++sets;return 0;}
static int llm_aff_id(void *p,uint64_t *midr,uint64_t *mpidr){
  (void)p;*midr=(uint64_t)(badpart?0:(actual<6?0xd05:0xd0b))<<4;
  *mpidr=(uint64_t)actual<<8;return 0;
}
static uint64_t llm_aff_time(void *p){(void)p;return now;}
static void llm_aff_wait(void *p){(void)p;++waits;now+=1000;if(!stuck && waits>=3)
  for(unsigned i=0;i<8;++i)if(mask==(1u<<i))actual=i;}
static int llm_aff_stop(void *p){(void)p;return stopped;}
FUNCTION
int main(void) {
  assert(!llm_affinity(7));assert(mask==128 && actual==7 && sets==1 && waits>=3);
  unsigned oldsets=sets,oldwait=waits;
  for(unsigned i=0;i<10000;++i)assert(!llm_affinity(7));
  assert(sets==oldsets && waits==oldwait);
  mask=255;assert(!llm_affinity(7));assert(sets==oldsets+1 && waits>oldwait);
  mask=255;actual=1;stuck=1;assert(llm_affinity(6)==-ETIMEDOUT);assert(mask==255);
  stuck=0;stopped=1;assert(llm_affinity(6)==-ECANCELED);assert(mask==255);
  stopped=0;badpart=1;assert(llm_affinity(7)<0);assert(mask==255);
  badpart=0;geterror=1;oldsets=sets;assert(llm_affinity(7)==-EIO && sets==oldsets);
  geterror=0;assert(!llm_affinity(5) && actual==5);
  puts("PASS: actual LLM qualifier; 10,000 warm matrices without re-pinning; delayed migration, A55/A76 identity, timeout/cancel rollback; numerical row loop byte-identical to v209. HOST MOCK, not board speed.");
}
""".replace('HEADER',str(header)).replace('FUNCTION',function)
with tempfile.TemporaryDirectory(prefix='llm210-') as directory:
    out=Path(directory);(out/'test.c').write_text(test)
    subprocess.run(['gcc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror',
        '-fsanitize=address,undefined',str(out/'test.c'),'-o',str(out/'test')],check=True)
    subprocess.run([str(out/'test')],check=True,timeout=10)
