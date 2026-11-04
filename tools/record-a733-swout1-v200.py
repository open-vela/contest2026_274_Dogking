#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Seal v200 startup lineage; reused firmware is audited, NOT re-built."""
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import shutil
import subprocess
import sys

repo=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
for name in ('check','package','seal','new_directory'):p.add_argument(name,type=Path)
a=p.parse_args()
if not __debug__:raise RuntimeError('Do not run with -O')
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
load=lambda path:json.loads(path.read_text())
check=load(a.check/'summary.json');package=load(a.package/'summary.json')
assert check['result']=='V200_SWOUT1_MODEL_PACKAGE_AND_BASE199_ARM_PASSED_NOT_BOARD_TESTED'
assert sha(a.check/'base/summary.json')==check['base_summary_sha256']
for name,value in check['source_sha256'].items():assert sha(repo/name)==value,('Stale source',name)
for name,value in check['primary_sha256'].items():assert sha(repo.parent/name)==value
for name,value in check['logs'].items():assert sha(a.check/name)==value
for name,value in check['source_sha256'].items():
    archived=subprocess.check_output(['git','show',package['packaging_commit']+':'+name],cwd=repo)
    assert hashlib.sha256(archived).hexdigest()==value,('Packaging source drift',name)
digest=runpy.run_path(str(repo/'tools/package-a733-boot-v188.py'))['digest']
assert digest(Path(package['image']))==package['sha256']
assert package['kernel_unchanged'] and package['kernel_release']==199
assert not package['physical_disks_written'] and not package['ufs_contents_written']
assert package['layout']['original_extlinux_preserved'] and package['layout']['active_extlinux_absent']
assert package['layout']['auto_script_matches_manual']
subprocess.run([sys.executable,str(repo/'tools/record-a733-camera-v192.py'),
                str(a.check/'base'),'out/camera-build-v199-r1',str(a.seal),
                'out/camera-package-v199-r1',str(a.package/'base199-recheck'),
                '--source-commit',package['firmware_source_commit'],'--release','199'],
               check=True,timeout=240,cwd=repo)
base=load(a.package/'base199-recheck/summary.json')
assert base['image']['kernel_sha256']==package['layout']['kernel_sha256']==sha(a.seal/'nuttx.bin')
assert base['image']['elf_sha256']==sha(a.seal/'nuttx')
assert package['power_gate']['kernel_sha256']==sha(a.seal/'nuttx.bin')
assert base['frame_reservation']['bytes']==4*1024*1024
a.new_directory.mkdir()
for name in ('summary.json','package.log','extlinux-original.conf'):
    shutil.copyfile(a.package/name,a.new_directory/('image-'+name))
for name in ('summary.json','elf-audit.json','sdk-restored.json','wrapper-status.txt'):
    shutil.copyfile(a.package/'base199-recheck'/name,a.new_directory/('base199-'+name))
result=dict(result='V200_BOOT_IMAGE_AND_REUSED_FIRMWARE_LINEAGE_PASSED_NOT_BOARD_TESTED',
            packaging_commit=package['packaging_commit'],firmware_source_commit=package['firmware_source_commit'],
            image_sha256=package['sha256'],image_bytes=package['bytes'],
            check_sha256=sha(a.check/'summary.json'),package_sha256=sha(a.package/'summary.json'),
            audit_source_sha256=sha(Path(__file__)),kernel_sha256=sha(a.seal/'nuttx.bin'),
            elf_sha256=sha(a.seal/'nuttx'),kernel_unchanged=True,new_firmware_build=False,
            frame_reservation=base['frame_reservation'],runtime_image_bytes=base['runtime_image_bytes'],
            prior_sdk_restoration=base['sdk_restore'],script_sha256=package['power_gate']['script_sha256'],
            auto_script_installed=True,auto_script_execution_hardware_verified=False,
            camera_raw_hardware_verified=False,hardware_verified=False)
(a.new_directory/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
print(result['result'])
