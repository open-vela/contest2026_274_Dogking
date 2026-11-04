#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import subprocess,tempfile,runpy
r=Path(__file__).resolve().parents[1];app=r/'board/a733-cubie-a7z/openvela-overlay/apps/system/aipet'
with tempfile.TemporaryDirectory(prefix='product-v227-') as d:
    t=Path(d);exe=t/'test'
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-g','-O1','-fsanitize=address,undefined',
      '-pthread','-I'+str(app),str(r/'tools/test-product-v227.cxx'),str(app/'mcu_protocol.cxx'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=10)
    policy=runpy.run_path(str(r/'tools/apply-cloud-latency-v227.py'))
    sdk=(r/'../../quickly-openvela').resolve();src=(sdk/'packages/ai_agent/src/llm/llm_proxy.c').read_text()
    # The real SDK file must be restored, and every staged mutation must be
    # inside BOTH wrapper backup/restore and byte-exact inventory coverage.
    wrapper=(r/'tools/build-a733-wapi.sh').read_text()
    patched=wrapper.split('patched_files=(',1)[1].split('\n)',1)[0].split()
    assert 'packages/ai_agent/src/llm/llm_proxy.c' in patched
    assert 'for path in "${patched_files[@]}"' in wrapper
    transformed=policy['transform'](src)
    assert transformed.count('desktop_cloud_latency(body, llm_host, model);')==2
    c=t/'policy.c';c.write_text('''#include <string.h>
#include <assert.h>
#include <cJSON.h>
#define CONFIG_A733_PRODUCT_V1 1
#define LOG_INFO 1
#define syslog(...) ((void)0)
'''+policy['HELPER']+'''
int main(void) {
 const char *host[]={"api.deepseek.com","api.mimo.xiaomi.com","api.deepseek.com.evil","api.deepseek.com"};
 const char *model[]={"deepseek-flash","deepseek-flash","deepseek-flash","deepseek-reasoner"};
 for(int i=0;i<4;i++){cJSON *b=cJSON_CreateObject();desktop_cloud_latency(b,host[i],model[i]);
 cJSON *v=cJSON_GetObjectItem(b,"thinking");if(!i)assert(v&&!strcmp(cJSON_GetObjectItem(v,"type")->valuestring,"disabled"));else assert(!v);cJSON_Delete(b);}
}
''')
    subprocess.run(['gcc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(sdk/'apps/netutils/cjson/cJSON'),str(c),str(sdk/'apps/netutils/cjson/cJSON/cJSON.c'),'-lm','-o',str(t/'policy')],check=True)
    subprocess.run([str(t/'policy')],check=True)
print('PASS real cloud latency request policy: official flash only, both request builders, other providers untouched; no paid calls.')
