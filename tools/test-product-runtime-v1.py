#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Execute real runtime cancellation/ownership code; MOCK camera, no hardware."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
app=r/'board/a733-cubie-a7z/openvela-overlay/apps/system/aipet'
source=r'''
#include <cassert>
#include <cstdio>
#include "product_runtime.cxx"
static int polls,stops,finished;
extern "C" int a733_display_pet_pause(void){return 0;}
extern "C" int a733_camera_identify_start(const char *,uint32_t *token){*token=7;return 0;}
extern "C" int a733_camera_identify_poll(uint32_t token,a733_live_result *){assert(token==7);return polls++?finished:-EAGAIN;}
extern "C" int a733_camera_identify_stop(uint32_t token){assert(token==7);++stops;return 0;}
static int cancel(void *){return -ECANCELED;}
int main(){
  assert(aipet::product_mode()==aipet::ProductMode::local);
  auto mode=[](const char *text){FILE *f=fopen(AIPET_PRODUCT_MODE_DIR "/product-mode","wb");assert(f);fputs(text,f);assert(!fclose(f));return aipet::product_mode();};
  assert(mode("cloud\n")==aipet::ProductMode::cloud);
  assert(mode("auto\n")==aipet::ProductMode::automatic);
  assert(mode("aut")==aipet::ProductMode::local);
  assert(mode("local\n")==aipet::ProductMode::local);
  std::string out="not published";
  for(int status:{-EOWNERDEAD,-ENODATA,-EPROTO}){
    polls=stops=0;finished=status;
    assert(aipet::product_capture_objects(out,cancel,nullptr)==(status==-EOWNERDEAD?status:-ECANCELED));
    assert(stops==1&&polls==2&&out=="not published");
  }
  sigset_t before,after;assert(!pthread_sigmask(SIG_SETMASK,nullptr,&before));
  {
    aipet::ProductInterruptGuard guard;assert(!guard.error&&!guard.cancelled());
    assert(!pthread_kill(pthread_self(),SIGINT));
    assert(guard.cancelled()==-ECANCELED);
    assert(!pthread_sigmask(SIG_SETMASK,nullptr,&after)&&sigismember(&after,SIGINT)==1);
  }
  assert(!pthread_sigmask(SIG_SETMASK,nullptr,&after));
  assert(sigismember(&before,SIGINT)==sigismember(&after,SIGINT));
  puts("PASS actual runtime: cleanup EOWNERDEAD overrides cancellation; cooperative SIGINT retained and drained after safe scope. MOCK camera, NOT board.");
}
'''
with tempfile.TemporaryDirectory(prefix='product-runtime-audit-') as d:
    t=Path(d);fixture=t/'runtime.cxx';fixture.write_text(source)
    cmd=['g++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections',
         '-Wl,--gc-sections','-fsanitize=address,undefined','-pthread',
         '-DAIPET_PRODUCT_MODE_DIR="'+str(t)+'"',
         '-I'+str(app),'-I'+str(app.parent/'a733display'),'-I'+str(app.parent.parent/'include'),
         '-I'+str(r/'../../quickly-openvela/packages/ai_agent/src'),
         '-I'+str(r/'../../quickly-openvela/packages/ai_agent/include'),
         str(fixture),str(app/'product_core.cxx'),'-o',str(t/'runtime')]
    subprocess.run(cmd,check=True,cwd=r)
    subprocess.run([str(t/'runtime')],check=True,timeout=15)
