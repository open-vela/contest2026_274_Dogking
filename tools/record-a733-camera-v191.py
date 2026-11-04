#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Verify and record a fully linked qualification image, NEVER hardware acceptance."""
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import shutil
import subprocess
import sys

repo = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('check', type=Path)
p.add_argument('build_report', type=Path)
p.add_argument('sdk_build', type=Path)
p.add_argument('package_report', type=Path)
p.add_argument('destination', type=Path)
p.add_argument('--source-commit', required=True)
a = p.parse_args()
if not __debug__:
    raise RuntimeError('Evidence recorder requires assertions; do not use -O')


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024*1024), b''):
            h.update(chunk)
    return h.hexdigest()


base = json.loads((a.check/'base/summary.json').read_text())
mode = json.loads((a.check/'summary.json').read_text())
for group in ('headers', 'test_sources', 'integration'):
    for name, expected in base[group].items():
        assert digest(repo/name) == expected, 'Stale host source: '+name
for item in base['files']:
    assert digest(repo/item['source']) == item['source_sha256']
    assert digest(a.check/'base'/(Path(item['source']).stem+'.o')) == item['object_sha256']
for name, expected in base['logs'].items():
    assert digest(a.check/'base'/name) == expected, 'Stale host log: '+name
for name, expected in mode['log_sha256'].items():
    assert digest(a.check/name) == expected, 'Stale mode log: '+name
assert digest(a.check/'a733_imx214_mode.o') == mode['mode_object_sha256']
assert digest(a.check/'a733_csi_frame_plan.o') == mode['frame_plan_object_sha256']
for name, expected in mode['source_sha256'].items():
    assert digest(repo/name) == expected, 'Stale mode source: '+name
assert (a.build_report/'wrapper-status.txt').read_text().strip() == '0'
restore = json.loads((a.build_report/'sdk-restored.json').read_text())
assert restore['result'] == 'SDK_RESTORED_BYTE_EXACT'
package = json.loads((a.package_report/'summary.json').read_text())
assert package['result'] == 'PRIVATE_CANDIDATE_PACKAGED_NOT_BOARD_TESTED'
assert package['purpose'] == 'camera-qualification'
assert package['camera_qualification_candidate']
assert not package['hardware_executed'] and not package['camera_capture']
assert not package['physical_camera_provider_bound']
for name, field in [('nuttx', 'elf_sha256'), ('nuttx.bin', 'kernel_sha256'),
                    ('.config', 'config_sha256')]:
    assert digest(a.sdk_build/name) == package[field], 'Build/package mismatch: '+name
assert digest(Path(package['image'])) == package['sha256']
audit = runpy.run_path(str(repo/'tools/audit-a733-boot-elf.py'))['audit'](a.sdk_build/'nuttx')
assert int(audit['layout']['_image_end'], 16) <= 0x48000000
assert len(audit['constructors']) == 24, 'Unexpected new global constructors'
profile_test = subprocess.run([sys.executable, str(repo/'tools/test-a733-camera-package-v191.py')],
                             check=True, capture_output=True, text=True, timeout=30)
source_names = [item['source'] for item in base['files']]
for name in source_names:
    committed = subprocess.check_output(['git', 'show', a.source_commit+':'+name], cwd=repo)
    assert hashlib.sha256(committed).hexdigest() == digest(repo/name), 'Firmware source drift: '+name
a.destination.mkdir()  # Never overwrite historical evidence.
for name in ('sdk-restored.json', 'wrapper-status.txt', 'build.log'):
    shutil.copyfile(a.build_report/name, a.destination/name)
for name in ('summary.json', 'package.log', 'boot-disassembly.txt'):
    shutil.copyfile(a.package_report/name, a.destination/('image-'+name))
(a.destination/'elf-audit.json').write_text(json.dumps(audit, indent=2)+'\n')
(a.destination/'package-profile-test.log').write_text(profile_test.stdout)
record = dict(result='CAMERA_V191_FULL_LINK_AND_IMAGE_CHECKS_PASSED_NOT_HARDWARE',
              firmware_source_commit=a.source_commit, image=package,
              source_check_sha256=digest(a.check/'summary.json'),
              baseline_check_sha256=digest(a.check/'base/summary.json'),
              firmware_source_sha256={name: digest(repo/name) for name in source_names},
              packaging_source_sha256={name: digest(repo/name) for name in (
                  'tools/package-a733-boot-v188.py', 'tools/test-a733-camera-package-v191.py',
                  'tools/build-a733-camera-v191.sh', 'tools/record-a733-camera-v191.py')},
              build_log_sha256=digest(a.build_report/'build.log'), sdk_restore=restore,
              runtime_image_bytes=int(audit['layout']['_image_end'],16)-0x40200000,
              constructor_count=len(audit['constructors']), full_firmware_linked=True,
              physical_camera_provider_bound=False, camera_capture=False,
              hardware_verified=False, old_v190_board_test='DEFERRED_BY_USER')
(a.destination/'summary.json').write_text(json.dumps(record, indent=2)+'\n')
print(record['result'], 'runtime-bytes=', record['runtime_image_bytes'])
