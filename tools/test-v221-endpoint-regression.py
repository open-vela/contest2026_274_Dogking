#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compare actual old/new gate on synthetic room-noise fluctuations, not audio proof."""
from pathlib import Path
import subprocess,tempfile,json,hashlib
r=Path(__file__).resolve().parents[1];p=r/'board/a733-cubie-a7z/openvela-overlay/apps/system/aipet/audio_capture_live.hxx'
old=subprocess.check_output(['git','show','a733-v1.2fix-v221-image-20261102:'+str(p.relative_to(r))],cwd=r)
body=r'''
#include "gate.hxx"
#include <cstdio>
static int sink(void *,const unsigned char *,std::size_t){return 0;}
int main(){aipet::DialogueGate gate;unsigned char b[960];int rc=0;unsigned i=0;
 for(;i<1000&&!gate.ended;i++){
  int amp=i<40?180:i<100?1000:(i%4?300:450);
  for(unsigned k=0;k<480;k++){unsigned v=uint16_t(int16_t((k%2?amp:-amp)+200));b[2*k]=v&255;b[2*k+1]=v>>8;}
  rc=gate.push(b,960,sink,nullptr);if(rc<0)return 2;
 }
 printf("%u %u %u %u\n",gate.ended,gate.limited,gate.emitted,i);
}
'''
result={}
with tempfile.TemporaryDirectory(prefix='v221-gate-') as tmp:
    d=Path(tmp);(d/'main.cxx').write_text(body)
    for name,data in [('old',old),('fix1',p.read_bytes())]:
        (d/'gate.hxx').write_bytes(data)
        subprocess.run(['g++','-std=c++17','-O1','-g','-fsanitize=address,undefined',str(d/'main.cxx'),'-o',str(d/'gate')],check=True)
        result[name]=list(map(int,subprocess.check_output([str(d/'gate')],text=True).split()))
assert result['old'][:2]==[1,1] and result['old'][2]==800,result
assert result['fix1'][:2]==[1,0] and result['fix1'][3]==170,result
result.update(hardware=False,source_sha256=hashlib.sha256(p.read_bytes()).hexdigest())
print(json.dumps(result,indent=2))
