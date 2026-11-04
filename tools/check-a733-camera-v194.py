#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Full earlier regressions + actual v194 standby mode/SHA/TWI composition."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
repo=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--build',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--evidence',type=Path)
a=p.parse_args();a.output.mkdir()
subprocess.run([sys.executable,str(repo/'tools/check-a733-camera-v193.py'),
    '--build',str(a.build),'--output',str(a.output/'base')],check=True,timeout=480)
for script in ('test-a733-camera-mode-trial.py','test-a733-camera-handoff-v194.py'):
    with (a.output/(script+'.log')).open('x') as log:
        subprocess.run([sys.executable,str(repo/'tools'/script)],stdout=log,
                       stderr=subprocess.STDOUT,check=True,timeout=90)
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
base=json.loads((a.output/'base/summary.json').read_text())
files=list(base['source_sha256'])+[
    'tools/check-a733-camera-v194.py','tools/a733_camera_test_build.py',
    'tools/build-a733-camera-v194.sh','tools/test-a733-camera-mode-trial.py',
    'tools/test-a733-camera-mode-trial.c','tools/test-a733-camera-handoff-v194.py',
    'tools/test-a733-camera-handoff-v192.sh']
result=dict(result='CAMERA_V194_HOST_ARM_STANDBY_MODE_PASSED_NOT_HARDWARE',
    source_sha256={s:sha(repo/s) for s in files},
    base_summary_sha256=sha(a.output/'base/summary.json'),
    logs={f.name:sha(f) for f in a.output.glob('*.log')},
    sha2_reference_sha256=sha(repo.parent.parent/'quickly-openvela/nuttx/crypto/sha2.c'),
    hardware_verified=False,camera_capture=False,receiver_dma_started=False,
    power_frequency_measurement=False,strict_qualification_created=False)
(a.output/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
if a.evidence:
    a.evidence.mkdir()
    for f in a.output.glob('*.log'):shutil.copyfile(f,a.evidence/f.name)
    (a.evidence/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
print(result['result'])
