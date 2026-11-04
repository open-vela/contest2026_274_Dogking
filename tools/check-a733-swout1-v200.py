#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""v200 boot-only change: rerun v199 host/ARM checks, plus power/package tests."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

repo=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
p.add_argument('--evidence',type=Path);a=p.parse_args();a.output.mkdir()
subprocess.run([sys.executable,str(repo/'tools/check-a733-camera-v199.py'),
                '--build',str(a.build),'--output',str(a.output/'base')],check=True,timeout=900)
for name in ('test-a733-swout1-v200.py','test-a733-swout1-package-v200.py'):
    with (a.output/(name+'.log')).open('x') as stream:
        subprocess.run([sys.executable,str(repo/'tools'/name)],stdout=stream,stderr=subprocess.STDOUT,
                       check=True,timeout=180)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
base=json.loads((a.output/'base/summary.json').read_text())
files=list(base['source_sha256'])+[
    'tools/build-a733-swout1-v200.py','tools/package-a733-swout1-v200.py',
    'tools/test-a733-swout1-v200.sh','tools/test-a733-swout1-v200.py',
    'tools/test-a733-swout1-package-v200.py','tools/check-a733-swout1-v200.py',
    'docs/evidence/swout1-v128-board-20261028/user-excerpt.txt']
primary=[
    'u-boot-2018-product-aiot-stable/drivers/sunxi_power/pmu_axp8191.c',
    'u-boot-2018-product-aiot-stable/include/sunxi_power/pmu_axp8191.h',
    'u-boot-2018-product-aiot-stable/include/config_distro_bootcmd.h']
result=dict(result='V200_SWOUT1_MODEL_PACKAGE_AND_BASE199_ARM_PASSED_NOT_BOARD_TESTED',
            source_sha256={name:sha(repo/name) for name in files},
            primary_sha256={name:sha(repo.parent/name) for name in primary},
            base_summary_sha256=sha(a.output/'base/summary.json'),
            logs={f.name:sha(f) for f in a.output.glob('*.log')},
            firmware_changed=False,firmware_release=199,
            hardware_verified=False,automatic_boot_verified=False,camera_raw_verified=False)
(a.output/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
if a.evidence:
    a.evidence.mkdir()
    for f in a.output.rglob('*'):
        if f.suffix in ('.json','.log','.su'):
            target=a.evidence/f.relative_to(a.output);target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(f,target)
print(result['result'])
