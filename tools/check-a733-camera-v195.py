#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import argparse,hashlib,json,shutil,subprocess,sys
from pathlib import Path
repo=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--build',required=True,type=Path)
p.add_argument('--output',required=True,type=Path);p.add_argument('--evidence',type=Path)
a=p.parse_args();a.output.mkdir()
with (a.output/'staging-tests.log').open('x') as f:
    subprocess.run([sys.executable,str(repo/'tools/test-a733-camera-staging.py')],
                   stdout=f,stderr=subprocess.STDOUT,check=True,timeout=15)
subprocess.run([sys.executable,str(repo/'tools/check-a733-camera-v194.py'),
    '--build',str(a.build),'--output',str(a.output/'base')],check=True,timeout=600)
with (a.output/'packet-tests.log').open('x') as f:
    subprocess.run([sys.executable,str(repo/'tools/test-a733-camera-link.py')],stdout=f,
                   stderr=subprocess.STDOUT,check=True,timeout=120)
with (a.output/'handoff-tests.log').open('x') as f:
    subprocess.run([sys.executable,str(repo/'tools/test-a733-camera-handoff-v194.py'),'195'],
                  stdout=f,stderr=subprocess.STDOUT,check=True,timeout=90)
base=json.loads((a.output/'base/summary.json').read_text())
original=json.loads((a.output/'base/base/base/summary.json').read_text())['files'][0]['command']
chip=repo/'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733'
obj=a.output/'a733_mipi_rx.o';cmd=list(original)
cmd[cmd.index('-c')+1]=str(chip/'a733_mipi_rx.c');cmd[cmd.index('-o')+1]=str(obj.resolve())
with (a.output/'arm-object.log').open('x') as f:
    subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=90)
frames=[int(s.rsplit('\t',2)[1]) for s in obj.with_suffix('.su').read_text().splitlines()]
assert max(frames)<=4096
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
files=list(base['source_sha256'])+[str((chip/n).relative_to(repo)) for n in ('a733_mipi_rx.c','a733_mipi_rx.h','CMakeLists.txt','Kconfig')]
files+=['tools/check-a733-camera-v195.py','tools/build-a733-camera-v195.sh',
        'tools/test-a733-camera-link.py','tools/test-a733-camera-link.c','tools/test-a733-mipi-rx.c',
        'tools/test-a733-camera-mode-trial.c','tools/record-a733-camera-v192.py',
        'tools/test-a733-camera-staging.py','tools/build-a733-wapi.sh','tools/check-a733-mcu-staging.py',
        'tools/verify-a733-camera-assets.sh']
result=dict(result='CAMERA_V195_HOST_ARM_PACKET_PATH_PASSED_NOT_HARDWARE',
    source_sha256={s:sha(repo/s) for s in files},base_summary_sha256=sha(a.output/'base/summary.json'),
    logs={f.name:sha(f) for f in a.output.glob('*.log')},object_sha256=sha(obj),
    maximum_stack_frame=max(frames),hardware_verified=False,camera_capture=False,ddr_dma=False)
(a.output/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
if a.evidence:
    a.evidence.mkdir()
    for f in a.output.glob('*.log'):shutil.copyfile(f,a.evidence/f.name)
    shutil.copyfile(a.output/'summary.json',a.evidence/'summary.json')
print(result['result'],max(frames))
