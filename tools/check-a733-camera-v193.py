#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Reuse earlier gate regressions, then check v193 composed ID experiment."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
repo=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--build',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--evidence',type=Path)
a=p.parse_args()
a.output.mkdir()
subprocess.run([sys.executable,str(repo/'tools/check-a733-camera-v192.py'),
    '--build',str(a.build),'--output',str(a.output/'base')],check=True,timeout=360)
with (a.output/'id-trial.log').open('x') as log:
    subprocess.run([sys.executable,str(repo/'tools/test-a733-imx214-trial.py')],
                   stdout=log,stderr=subprocess.STDOUT,check=True,timeout=90)
base=json.loads((a.output/'base/summary.json').read_text())
sources=list(base['source_sha256'])+[
    'tools/check-a733-camera-v193.py','tools/test-a733-imx214-trial.py',
    'tools/test-a733-imx214-trial.c','tools/build-a733-camera-v193.sh',
    'tools/package-a733-boot-v188.py','tools/package-a733-kernel-image.sh']
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
result=dict(result='CAMERA_V193_HOST_AND_ARM_OBJECTS_PASSED_NOT_HARDWARE',
    source_sha256={s:sha(repo/s) for s in sources},
    base_summary_sha256=sha(a.output/'base/summary.json'),
    id_trial_log_sha256=sha(a.output/'id-trial.log'),
    hardware_verified=False,sensor_id_read_on_hardware=False,camera_capture=False,
    strict_qualification_created=False)
(a.output/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
if a.evidence:
    import shutil
    a.evidence.mkdir()
    shutil.copyfile(a.output/'id-trial.log',a.evidence/'id-trial.log')
    (a.evidence/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
print(result['result'])
