#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Validate v187 sensor mode source and private config; never execute hardware."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--mode', type=Path, required=True)
p.add_argument('--evidence', type=Path)
a = p.parse_args()
repo = Path(__file__).resolve().parent.parent
chip = repo / 'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733'
out = a.output.resolve()
out.mkdir()
sha = lambda f: hashlib.sha256(f.read_bytes()).hexdigest()
subprocess.run([sys.executable, str(repo/'tools/check-a733-camera-v186.py'),
    '--build',str(a.build),'--output',str(out/'base')],check=True,timeout=180)
base = json.loads((out/'base/summary.json').read_text())
source = chip/'a733_imx214_mode.c'
obj = out/'a733_imx214_mode.o'
command = list(base['files'][0]['command'])
command[command.index('-c')+1] = str(source)
command[command.index('-o')+1] = str(obj)
with (out/'arm.log').open('xb') as log:
    subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
maximum = 0
for line in obj.with_suffix('.su').read_text().splitlines():
    _, size, kind = line.rsplit('\t',2)
    if kind!='static' or not size.isdigit() or int(size)>4096: raise ValueError(line)
    maximum=max(maximum,int(size))
plan_source = chip/'a733_csi_frame_plan.c'
plan_obj = out/'a733_csi_frame_plan.o'
plan_command = list(command)
plan_command[plan_command.index('-c')+1] = str(plan_source)
plan_command[plan_command.index('-o')+1] = str(plan_obj)
with (out/'frame-plan-arm.log').open('xb') as log:
    subprocess.run(plan_command,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
plan_maximum=0
for line in plan_obj.with_suffix('.su').read_text().splitlines():
    _, size, kind=line.rsplit('\t',2)
    if kind!='static' or not size.isdigit() or int(size)>4096: raise ValueError(line)
    plan_maximum=max(plan_maximum,int(size))
with (out/'frame-plan-host.log').open('xb') as log:
    subprocess.run([sys.executable,str(repo/'tools/test-a733-csi-frame-plan.py')],
        stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
manifest = json.loads(a.mode.with_name('manifest.json').read_text())
assert sha(a.mode)==manifest['artifact_sha256']
assert manifest['artifact_sha256']=='a7b2102ba05c51de1a8a7602152505f4feedbeb3ec9facdda70710f9639bb2af'
assert manifest['source_sha256']=='e05a71fe4843c81d354ab9fa30bbd809796c21d557a87af54b550ba4e4a23832'
assert manifest['bytes']==660 and manifest['operations']==153
assert manifest['source_spdx']=='GPL-2.0-or-later'
assert not manifest['hardware_verified'] and not manifest['sensor_execution']
with (out/'mode-host.log').open('xb') as log:
    subprocess.run([sys.executable,str(repo/'tools/test-a733-imx214-mode.py'),
        str(a.mode.resolve())],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
files = [source, source.with_suffix('.h'), chip/'a733_imx214_twi3.c',
         chip/'a733_imx214_twi3.h', repo/'tools/export-a733-imx214-mode.py',
         repo/'tools/test-a733-imx214-mode.c',repo/'tools/test-a733-imx214-mode.py',
         repo/'tools/check-a733-camera-v187.py',plan_source,plan_source.with_suffix('.h'),
         repo/'tools/test-a733-csi-frame-plan.c',repo/'tools/test-a733-csi-frame-plan.py']
record = dict(result='CAMERA_V187_SOURCE_AND_SYNTHETIC_TESTS_PASSED',
    baseline_commit='a92c716',mode_object_sha256=sha(obj),
    maximum_mode_stack_frame=maximum,
    frame_plan_object_sha256=sha(plan_obj),maximum_frame_plan_stack_frame=plan_maximum,
    source_sha256={str(f.relative_to(repo)):sha(f) for f in files},
    private_reference=manifest,full_firmware_linked=False,sdk_modified=False,
    physical_provider_bound=False,sensor_executed=False,camera_capture=False,
    dma_executed=False,hardware_verified=False,
    base_tests=[f.name for f in sorted((out/'base').glob('test-*.log'))],
    log_sha256={f.name:sha(f) for f in sorted(out.glob('*.log'))})
reference_root = repo.parent/'bsp-product-aiot-stable/drivers/vin'
reference_names = ['vin-video/vin_core.c','vin-video/vin_video.c',
    'vin-video/dma140/dma140_reg.c','vin-video/dma140/dma140_reg_i.h',
    'platform/platform_cfg.h','platform/sun60iw2_vin_cfg.h']
record['vin_reference_sha256'] = {name:sha(reference_root/name) for name in reference_names}
(out/'command.json').write_text(json.dumps(command,indent=2)+'\n')
(out/'summary.json').write_text(json.dumps(record,indent=2)+'\n')
if a.evidence:
    a.evidence.mkdir()
    shutil.copyfile(out/'summary.json',a.evidence/'summary.json')
    shutil.copyfile(out/'mode-host.log',a.evidence/'mode-host.log')
    shutil.copyfile(out/'frame-plan-host.log',a.evidence/'frame-plan-host.log')
    for log in sorted((out/'base').glob('test-*.log')):
        shutil.copyfile(log,a.evidence/log.name)
    # The GPL-derived binary/source stays private in out/. This only records
    # the original per-source AArch64 validation facts, not its command paths.
    for item in base['files']: item.pop('command',None)
    (a.evidence/'baseline-check.json').write_text(json.dumps(base,indent=2)+'\n')
print(record['result'],'mode-stack=',maximum,
      'private-config-ops=',manifest['operations'],'HARDWARE=NOT_TESTED')
