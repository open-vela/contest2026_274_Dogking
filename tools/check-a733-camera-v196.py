#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import argparse,hashlib,json,shutil,subprocess,sys
from pathlib import Path
repo=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--build',required=True,type=Path)
p.add_argument('--output',required=True,type=Path);p.add_argument('--evidence',type=Path)
a=p.parse_args();a.output.mkdir()
subprocess.run([sys.executable,str(repo/'tools/check-a733-camera-v195.py'),
    '--build',str(a.build),'--output',str(a.output/'base')],check=True,timeout=660)
for name,args in (('register-model.log',['test-a733-mipi-registers.py']),
                  ('handoff-v196.log',['test-a733-camera-handoff-v194.py','196'])):
    with (a.output/name).open('x') as f:
        subprocess.run([sys.executable,str(repo/'tools'/args[0]),*args[1:]],
                       stdout=f,stderr=subprocess.STDOUT,check=True,timeout=90)
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
base=json.loads((a.output/'base/summary.json').read_text())
files=list(base['source_sha256'])+['tools/check-a733-camera-v196.py',
    'tools/build-a733-camera-v196.sh','tools/test-a733-mipi-registers.py']
primary=repo.parent/'bsp-product-aiot-stable/drivers/vin/vin-mipi/combo_csi/combo_csi_reg_i.h'
references=[primary,primary.parent/'combo_csi_reg.c',primary.parents[2]/'utility/vin_io.h']
result=dict(result='CAMERA_V196_REGISTER_SEMANTICS_AND_LEGACY_REPLAY_PASSED_NOT_HARDWARE',
    source_sha256={s:sha(repo/s) for s in files},base_summary_sha256=sha(a.output/'base/summary.json'),
    logs={f.name:sha(f) for f in a.output.glob('*.log')},primary_header_sha256=sha(primary),
    primary_reference_sha256={str(f.relative_to(repo.parent)):sha(f) for f in references},
    legacy_source_commit='92363e6574d9f3eb54a0fbf08488f0cd05a3d940',
    hardware_verified=False,camera_capture=False,ddr_dma=False)
(a.output/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
if a.evidence:
    a.evidence.mkdir()
    for f in a.output.glob('*.log'):shutil.copyfile(f,a.evidence/f.name)
    shutil.copyfile(a.output/'summary.json',a.evidence/'summary.json')
print(result['result'])
