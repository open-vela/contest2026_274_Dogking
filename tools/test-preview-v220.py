#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Real profile/config code + actual prefill allocator; mocked engines only."""
from pathlib import Path
import subprocess, tempfile
r=Path(__file__).resolve().parents[1]
app=r/'board/a733-cubie-a7z/openvela-overlay/apps/system/aipet'
llm=app.parent/'aipetllm'
with tempfile.TemporaryDirectory(prefix='preview-v220-') as tmp:
    t=Path(tmp)
    for preview in (False,True):
        state=t/('preview' if preview else 'stable');state.mkdir()
        exe=t/('preview-test' if preview else 'stable-test')
        cmd=['g++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror',
             '-fsanitize=address,undefined','-pthread',f'-DAIPET_LOCAL_DIR="{state}/state"',
             '-I'+str(r/'tools/llm-host-config'),'-I'+str(app),'-I'+str(llm)]
        if preview:cmd+=['-DCONFIG_A733_PRODUCT_V12=1']
        cmd += [str(r/'tools'/('test-local-preview-v220.cxx' if preview else 'test-local-llm-config.cxx')),
                str(app/'local_llm.cxx'),str(app/'pet_core.cxx'),'-o',str(exe)]
        subprocess.run(cmd,check=True)
        subprocess.run([str(exe),str(state)],check=True,timeout=30)
    source=(llm/'aipetllm_modelcheck.c').read_text()
    helper=source[source.index('#define AIPETLLM_PREFILL_CACHE_LIMIT'):].split('#endif',1)[0]
    c=t/'scratch.c'
    c.write_text('''#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static int fail_alloc,freed;
static void *test_calloc(size_t n,size_t size){return fail_alloc?NULL:calloc(n,size);}
static void test_free(void *);
#define calloc test_calloc
#define free test_free
'''+helper+'''
#undef calloc
#undef free
static void test_free(void *p){if(p){++freed;free(p);}}
int main(void){
  assert(!prefill_acquire(0) && !prefill_acquire(SIZE_MAX));
  float *a=prefill_acquire(1024);assert(a && g_prefill_scratch.allocations==1);
  for(unsigned i=0;i<1024;i++)a[i]=3.0f;
  prefill_release(a);assert(!freed);
  float *b=prefill_acquire(100);assert(a==b && g_prefill_scratch.reuses==1);
  for(unsigned i=0;i<1024;i++)assert(b[i]==0);
  fail_alloc=1;assert(!prefill_acquire(2048));
  assert(g_prefill_scratch.data==a && g_prefill_scratch.bytes==4096);
  fail_alloc=0;b=prefill_acquire(2048);assert(b && freed==1);
  float *large=prefill_acquire(AIPETLLM_PREFILL_CACHE_LIMIT/4+1);assert(large && large!=b);
  assert(g_prefill_scratch.bytes==8192);prefill_release(large);assert(freed==2);
  prefill_unload();assert(freed==3 && !g_prefill_scratch.data && !g_prefill_scratch.bytes);
  prefill_unload();assert(freed==3);return 0;
}
''')
    exe=t/'scratch'
    subprocess.run(['gcc','-O1','-g','-std=c11','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=30)
print('V220_PREVIEW_PROFILE_AND_REAL_ALLOCATOR_PASSED_NOT_BOARD')
