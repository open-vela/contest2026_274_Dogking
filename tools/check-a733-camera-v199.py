#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import argparse,hashlib,json,shutil,subprocess,sys
from pathlib import Path
repo=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--build',required=True,type=Path)
p.add_argument('--output',required=True,type=Path);p.add_argument('--evidence',type=Path)
a=p.parse_args();a.output.mkdir()
subprocess.run([sys.executable,str(repo/'tools/check-a733-camera-v198.py'),
    '--build',str(a.build),'--output',str(a.output/'base')],check=True,timeout=900)
for script,args in [('test-a733-csi-registers-v199.py',[]),('test-a733-camera-handoff-v194.py',['199'])]:
    with (a.output/(script+'.log')).open('x') as f:
        subprocess.run([sys.executable,str(repo/'tools'/script),*args],stdout=f,
                       stderr=subprocess.STDOUT,check=True,timeout=120)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
base=json.loads((a.output/'base/summary.json').read_text())
files=list(base['source_sha256'])+['tools/check-a733-camera-v199.py','tools/build-a733-camera-v199.sh',
    'tools/test-a733-csi-registers-v199.py','tools/csi-size-host-model.h']
for name,expected in [('v197-serial.txt','4ec97f9bd64f98c9497f724d748fa93d31349163b98a90de9e3d21fe59651a15'),
                      ('v198-serial.txt','25f3ec0361b8f924c773e15f4cfa39ee0e09d87bc86af849084aa8f11fb82447')]:
    path='docs/evidence/camera-v197-v198-board-20261028/'+name
    assert sha(repo/path)==expected;files.append(path)
result=dict(result='CAMERA_V199_AUTO_SIZING_REPAIR_HOST_AND_ARM_PASSED_NOT_HARDWARE',
    source_sha256={n:sha(repo/n) for n in files},base_summary_sha256=sha(a.output/'base/summary.json'),
    logs={f.name:sha(f) for f in a.output.glob('*.log')},hardware_verified=False,
    v197_reported_fault_reproduced_in_model=True,v198_missing_handoff_preserved_zero_MMIO=True)
(a.output/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
if a.evidence:
    a.evidence.mkdir()
    for f in a.output.rglob('*'):
        if f.suffix in ('.log','.json','.su'):
            dst=a.evidence/f.relative_to(a.output);dst.parent.mkdir(exist_ok=True,parents=True);shutil.copyfile(f,dst)
print(result['result'])
