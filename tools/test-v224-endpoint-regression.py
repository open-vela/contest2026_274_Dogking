#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Same synthetic impulses through sealed v223 and current production headers."""
from pathlib import Path
import subprocess,tempfile,json,hashlib
r=Path(__file__).resolve().parents[1];p=r/'board/a733-cubie-a7z/openvela-overlay/apps/system/aipet/audio_capture_live.hxx'
old=subprocess.check_output(['git','show','a733-v1.3fix-v223-image-20261102:'+str(p.relative_to(r))],cwd=r)
body=r'''
#include "gate.hxx"
#include <cstdio>
static int sink(void *,const unsigned char *,std::size_t){return 0;}
int main(){aipet::DialogueGate gate;unsigned char b[960];unsigned i=0;
 for(;i<1200&&!gate.ended;i++){
  int amp=i<40?150:i<100?1000:(i%5?250:600);
  for(unsigned k=0;k<480;k++){unsigned v=uint16_t(int16_t((k%2?amp:-amp)+200));b[2*k]=v&255;b[2*k+1]=v>>8;}
  if(gate.push(b,960,sink,nullptr)<0)return 2;
 }
 printf("%u %u %u %u\n",gate.ended,gate.limited,gate.emitted,i);
}
'''
result={}
with tempfile.TemporaryDirectory(prefix='v224-gate-') as tmp:
    d=Path(tmp);(d/'main.cxx').write_text(body)
    for name,data in [('v223',old),('v224',p.read_bytes())]:
        (d/'gate.hxx').write_bytes(data)
        subprocess.run(['g++','-std=c++17','-O1','-g','-fsanitize=address,undefined',str(d/'main.cxx'),'-o',str(d/'gate')],check=True)
        result[name]=list(map(int,subprocess.check_output([str(d/'gate')],text=True).split()))
assert result['v223'][:3]==[1,1,800],result
assert result['v224'][:3]==[1,0,172],result
result.update(hardware=False,real_board_PCM=False,source_sha256=hashlib.sha256(p.read_bytes()).hexdigest())
print(json.dumps(result,indent=2))
