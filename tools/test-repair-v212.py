#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Execute production deadline, tensor ownership and I2S setup functions.
All scheduling/MMIO is synthetic; never an acoustic/board acceptance claim.
"""
from pathlib import Path
import re,subprocess,tempfile
r=Path(__file__).resolve().parents[1]
base=r/'board/a733-cubie-a7z/openvela-overlay'
llm=(base/'apps/system/aipetllm/aipetllm_modelcheck.c').read_text()
audio=(base/'vendor/allwinnertech/chips/a733/a733_i2s0_audio.c').read_text()
def fn(source,name):
    start=source.index('static ',source.index(name)-40)
    op=source.index('\n{',start)
    return source[start:source.index('\n}',op)+2]
def run(name,source):
    with tempfile.TemporaryDirectory(prefix='repair212-'+name) as d:
        p=Path(d);(p/'test.c').write_text(source)
        subprocess.run(['gcc','-D_GNU_SOURCE','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror',
            '-fsanitize=address,undefined','-pthread',str(p/'test.c'),'-o',str(p/'test')],check=True)
        subprocess.run([str(p/'test')],check=True,timeout=20)
common='''
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
'''
run('deadline',common+r'''
static int g_pool_fault,mode,calls,snapshots;
static sem_t g_pool_reply;
static int mock_time(clockid_t clock,struct timespec *t){assert(clock==CLOCK_MONOTONIC);t->tv_sec=100;t->tv_nsec=0;return 0;}
static int mock_wait(sem_t *s,clockid_t clock,const struct timespec *t){
 assert(s==&g_pool_reply&&clock==CLOCK_MONOTONIC&&t->tv_sec==115);++calls;
 if(mode==0)return 0;
 if(mode==1&&calls==1){errno=EINTR;return -1;}
 errno=mode==2?EIO:ETIMEDOUT;return -1;
}
static void pool_snapshot(const char *stage,int error){assert(!strcmp(stage,"test")&&error<0);++snapshots;}
#define clock_gettime mock_time
#define sem_clockwait mock_wait
'''+fn(llm,'pool_reply(')+r'''
int main(void){
 assert(!pool_reply("test")&&!g_pool_fault);
 mode=1;calls=0;assert(pool_reply("test")==-ETIMEDOUT&&calls==2&&snapshots==1);
 assert(g_pool_fault==-ETIMEDOUT);mode=0;assert(pool_reply("test")==-ETIMEDOUT);
 g_pool_fault=0;mode=2;assert(pool_reply("test")==-EIO&&g_pool_fault==-EIO);
 puts("PASS v212 actual reply deadline: monotonic, EINTR retry, timeout/error quarantine, no false success");
}
''')
work=re.search(r'struct matvec_worker_s\n\{.*?\n\};',llm,re.S)[0]
slots=re.search(r'struct pool_slot_s\n\{.*?\n\};',llm,re.S)[0]
run('ownership',common+r'''
#define GGML_TYPE_F32 0
#define GGML_TYPE_Q4_K 1
#define GGML_TYPE_Q6_K 2
#define QK_K 256
#define AIPETLLM_Q8_CACHE_LIMIT 262144
typedef struct {unsigned char data[32];} block_q8_K;
typedef struct {unsigned char data[32];} block_q4_K;
struct tensor_info_s {unsigned dimension_count,type;uint64_t dimensions[2];};
'''+work+slots+r'''
static int g_pool_fault,g_pool_count=2,g_pool_result,g_pool_command,fail_job;
static struct pool_slot_s g_pool_slots[8];
static sem_t g_pool_request;
static float *g_pool_output;
static size_t g_pool_output_bytes;
static int g_q8_workspace;
static unsigned char q8[4096];
static int aipetllm_q8_reserve(int *p,size_t n,void **v){assert(p==&g_q8_workspace&&n<=sizeof(q8));*v=q8;return 0;}
static int read_quantized_matvec(FILE *f,uint64_t o,const struct tensor_info_s *t,const float *i,float *v){(void)f;(void)o;(void)t;(void)i;(void)v;return -ENOTSUP;}
static int read_exact(FILE *f,void *d,size_t n){return fread(d,1,n,f)==n?0:-EIO;}
static int tensor_bytes(const uint64_t *d,unsigned n,unsigned type,uint64_t *v){assert(*d==256&&n==1&&type==1);*v=32;return 0;}
static void quantize_row_q8_K(const float *i,block_q8_K *o,int64_t n){assert(i&&n==256);memset(o,7,sizeof(*o));}
static int llm_pool_configure(unsigned m){assert(m==192);g_pool_slots[0].work.cpu=6;g_pool_slots[1].work.cpu=7;return 0;}
static int pool_reply(const char *stage){
 assert(!strcmp(stage,"matrix")&&g_pool_command==2);
 for(unsigned j=0;j<8;j++)g_pool_output[j]=100+j;
 if(fail_job){g_pool_fault=-ETIMEDOUT;return -ETIMEDOUT;}return 0;
}
'''+fn(llm,'read_parallel_matvec_batch(')+r'''
int main(void){
 unsigned char model[64]={0};float input[1024]={0},out[8];
 struct tensor_info_s t={2,1,{256,2}};
 for(unsigned j=0;j<8;j++)out[j]=-1;
 assert(!sem_init(&g_pool_request,0,0));
 assert(!read_parallel_matvec_batch(NULL,0,&t,input,out,192,model,sizeof(model),4));
 for(unsigned j=0;j<8;j++)assert(out[j]==100+j);
 for(unsigned j=0;j<8;j++)out[j]=-1;
 fail_job=1;assert(read_parallel_matvec_batch(NULL,0,&t,input,out,192,model,sizeof(model),4)==-ETIMEDOUT);
 for(unsigned j=0;j<8;j++)assert(out[j]==-1);
 assert(g_pool_slots[0].work.output==g_pool_output&&g_pool_slots[0].work.output!=out);
 assert(g_pool_slots[0].work.input==(void *)q8&&q8[0]==7);
 assert(read_parallel_matvec_batch(NULL,0,&t,input,out,192,model,sizeof(model),4)==-EOWNERDEAD);
 /* No real workers in this fixture; only the test may free retained scratch. */
 free(g_pool_output);sem_destroy(&g_pool_request);
 puts("PASS v212 actual matrix dispatcher: success copies all output; timeout publishes nothing; output/input stay worker-owned; future jobs blocked");
}
''')
defs='\n'.join(x for x in audio.replace('\\\n','').splitlines()
               if re.match(r'#define (?:A733_|I2S_|PLL_AUDIO1_)',x))
diag=re.search(r'struct a733_i2s0_diag_s\n\{.*?\n\};',audio,re.S)[0]
helpers='\n'.join(fn(audio,n) for n in ('a733_pb_pinmux(','a733_i2s0_set_sample_resolution(',
    'a733_i2s0_set_clock_polarity(','a733_i2s0_tx_prepare(','a733_i2s0_tx_sample(','a733_i2s0_tx_begin('))
run('tx',common+'typedef int mutex_t;\n'+defs+'\n'+diag+r'''
static struct a733_i2s0_diag_s g_i2s0;
static uintptr_t addresses[100];static uint32_t values[100];static unsigned used;static int bad_pin;
static unsigned index_for(uintptr_t a){for(unsigned i=0;i<used;i++)if(addresses[i]==a)return i;assert(used<100);addresses[used]=a;return used++;}
static uint32_t getreg32(uintptr_t a){uint32_t v=values[index_for(a)];return bad_pin&&a==A733_PB_BASE?v^0x10000000:v;}
static void putreg32(uint32_t v,uintptr_t a){values[index_for(a)]=v;}
static int a733_i2s0_hw_initialize(void){return 0;}
static void a733_i2s0_stream_stop(void){putreg32(0,A733_I2S0_BASE+I2S_CTL);}
'''+helpers+r'''
int main(void){
 putreg32(PLL_AUDIO1_LOCK,A733_PLL_AUDIO1);putreg32(0xabcdef01,A733_PB_BASE);
 putreg32(0xfeed4321,A733_I2S0_BASE+I2S_CHCFG);putreg32(0x9876,A733_PB_BASE+4);
 putreg32(I2S_FMT0_LRCK_32|I2S_FMT0_SLOT_32,A733_I2S0_BASE+I2S_FMT0);
 assert(!a733_i2s0_tx_prepare(true));assert(!a733_i2s0_tx_begin());
 assert((g_i2s0.tx_active_cfg&0xfff00000)==0x33300000);
 assert((g_i2s0.tx_active_cfg&0xfffff)==0xdef01);
 assert(getreg32(A733_PB_BASE+4)==0x9876);
 assert(getreg32(A733_I2S0_BASE+I2S_CHCFG)==0xfeed4321);
 assert(getreg32(A733_I2S0_BASE+I2S_TX0CHMAP1)==0x10);
 a733_i2s0_tx_sample(0);a733_i2s0_tx_sample(-32768);a733_i2s0_tx_sample(123);
 assert(g_i2s0.tx_samples==3&&g_i2s0.tx_nonzero==2&&g_i2s0.tx_peak==32768);
 bad_pin=1;assert(a733_i2s0_tx_begin()==-EIO&&getreg32(A733_I2S0_BASE+I2S_CTL)==0);
 puts("PASS v212 actual TX setup/pinmux/start: full profile restoration, unrelated pins/RX preserved, nonzero/peak proof, bad readback stops clocks; MOCK ONLY");
}
''')
print('PASS v212 repair gates; NOT board scheduling/acoustic proof')
