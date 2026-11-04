#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Host sanitizers and real SDK AArch64 object checks. No SDK staging/HW."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
repo = Path(__file__).resolve().parent.parent
overlay = repo / 'board/a733-cubie-a7z/openvela-overlay'
chip = overlay / 'vendor/allwinnertech/chips/a733'
board = overlay / 'vendor/allwinnertech/boards/a733/cubie-a7z/src'
apps = overlay / 'apps'
out = a.output.resolve()
out.mkdir()  # Never replace previous evidence.
sha = lambda file: hashlib.sha256(file.read_bytes()).hexdigest()
db = json.loads((a.build / 'compile_commands.json').read_text())
entry = next(e for e in db if e['file'].endswith('/a733_ufs_data.c'))
args = shlex.split(entry['command'])
base = [args[0]]
i = 1
while i < len(args):
    if args[i] in ('-c', '-o', '-MF', '-MT', '-MQ'):
        i += 2
    elif args[i] in ('-MD', '-MMD'):
        i += 1
    else:
        base.append(args[i])
        i += 1
records = []
sources = [chip / 'a733_camera_board.c', chip / 'a733_camera_diag.c',
           chip / 'a733_imx214_probe.c', chip / 'a733_imx214_twi3.c',
           board / 'a7z_boardinit.c',
           apps / 'system/a733camera/a733_camera_image.c',
           apps / 'system/a733camera/a733camera_main.c']
for source in sources:
    obj = out / (source.stem + '.o')
    command = base + ['-DCONFIG_A733_IMX214_CHECKPOINT=1',
        '-I' + str(chip), '-I' + str(apps / 'include'),
        '-include', str(chip / 'include/a733_peripherals.h'),
        '-fstack-usage', '-Werror=frame-larger-than=4096',
        '-Wframe-larger-than=4096', '-Werror=implicit-function-declaration',
        '-c', str(source), '-o', str(obj)]
    with (out / (source.stem + '.log')).open('xb') as log:
        subprocess.run(command, cwd=entry['directory'], stdout=log,
                       stderr=subprocess.STDOUT, check=True, timeout=120)
    frames = []
    for line in obj.with_suffix('.su').read_text().splitlines():
        _, size, kind = line.rsplit('\t', 2)
        if kind != 'static' or not size.isdigit() or int(size) > 4096:
            raise ValueError(line)
        frames.append(int(size))
    records.append(dict(source=str(source.relative_to(repo)),
        source_sha256=sha(source), object_sha256=sha(obj),
        maximum_stack_frame=max(frames, default=0), command=command))
for name in ('test-a733-camera-board.py', 'test-a733-camera-image.py',
             'test-a733-imx214-probe.py', 'test-linux-imx214-script.py',
             'test-a733-camera-cli.py', 'test-a733-imx214-twi3.py',
             'test-a733-camera-diag.py'):
    with (out / (name + '.log')).open('xb') as log:
        subprocess.run([sys.executable, str(repo / 'tools' / name)],
            cwd=repo, stdout=log, stderr=subprocess.STDOUT,
            check=True, timeout=60)
headers = [chip / 'a733_camera_board.h', chip / 'a733_imx214_probe.h',
           chip / 'a733_imx214_twi3.h',
           chip / 'include/a733_peripherals.h',
           apps / 'include/system/a733_camera_image.h']
test_files = list((repo / 'tools').glob('test-a733-camera-*'))
test_files += list((repo / 'tools').glob('test-a733-imx214-*'))
test_files += [repo / 'tools/test-linux-imx214-script.py',
               repo / 'tools/test-linux-imx214.sh']
test_files += list((repo / 'tools/camera-host-fixture').rglob('*.h'))
integration = [chip / 'Kconfig', chip / 'CMakeLists.txt',
               repo / 'tools/build-a733-wapi.sh']
integration += [apps / 'system/a733camera' / name for name in
                ('CMakeLists.txt', 'Kconfig', 'Make.defs', 'Makefile')]
with (out / 'build-wrapper-syntax.log').open('xb') as log:
    subprocess.run(['bash', '-n', str(repo / 'tools/build-a733-wapi.sh')],
        stdout=log, stderr=subprocess.STDOUT, check=True, timeout=10)
summary = dict(result='CAMERA_V186_ARM_OBJECTS_AND_SYNTHETIC_TESTS_PASSED',
    build=str(a.build.resolve()), files=records,
    headers={str(h.relative_to(repo)): sha(h) for h in headers},
    test_sources={str(f.relative_to(repo)): sha(f) for f in sorted(test_files)},
    integration={str(f.relative_to(repo)): sha(f) for f in integration},
    logs={f.name: sha(f) for f in sorted(out.glob('*.log'))},
    full_firmware_linked=False, sensor_provider=False, camera_capture=False,
    npu_executed=False, hardware_verified=False, sdk_modified=False)
(out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
print(summary['result'])
for r in records:
    print(r['source'], 'stack=', r['maximum_stack_frame'])
