#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Source/host/AArch64 qualification only, not a firmware or hardware run."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

repo=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--build',required=True,type=Path)
p.add_argument('--output',required=True,type=Path)
p.add_argument('--evidence',type=Path)
a=p.parse_args()
a.output.mkdir()
subprocess.run([sys.executable,str(repo/'tools/check-a733-camera-v187.py'),
    '--build',str(a.build),'--output',str(a.output/'base'),
    '--mode',str(repo/'out/imx214-mode-v187-r1/imx214-1080p.a7cm')],check=True,timeout=240)
base=json.loads((a.output/'base/base/summary.json').read_text())
chip=repo/'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733'
apps=repo/'board/a733-cubie-a7z/openvela-overlay/apps'
sources=[chip/'a733_imx214_cold.c',chip/'a733_camera_control.c',
         chip/'a733_camera_diag.c',apps/'system/a733camera/a733camera_main.c']
records=[]
for source in sources:
    obj=a.output/(source.stem+'.o')
    cmd=list(base['files'][0]['command'])
    cmd[cmd.index('-c')+1]=str(source.resolve())
    cmd[cmd.index('-o')+1]=str(obj.resolve())
    cmd+=['-DCONFIG_A733_CAMERA_CONTROL_TEST=1']
    with (a.output/(source.stem+'.log')).open('xb') as log:
        subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=90)
    frames=[]
    for line in obj.with_suffix('.su').read_text().splitlines():
        _,n,kind=line.rsplit('\t',2)
        if kind!='static' or not n.isdigit() or int(n)>4096:raise ValueError(line)
        frames.append(int(n))
    records.append(dict(source=str(source.relative_to(repo)),
                        maximum_stack_frame=max(frames,default=0),
                        object_sha256=hashlib.sha256(obj.read_bytes()).hexdigest(),command=cmd))
for script in ('test-a733-imx214-cold.py','test-a733-camera-control.py',
               'test-a733-camera-handoff-v192.py'):
    with (a.output/(script+'.log')).open('xb') as log:
        subprocess.run([sys.executable,str(repo/'tools'/script)],
                       stdout=log,stderr=subprocess.STDOUT,check=True,timeout=90)
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
files=sources+[chip/'a733_imx214_cold.h',chip/'a733_camera_power_handoff.h',
    repo/'tools/build-a733-camera-v192.sh',repo/'tools/build-a733-camera-handoff-v192.py',
    repo/'tools/test-a733-imx214-cold.c',repo/'tools/test-a733-camera-control.c',
    repo/'tools/test-a733-camera-handoff-v192.py']
files += [repo/'tools/check-a733-camera-v192.py',repo/'tools/test-a733-camera-control.py',
          repo/'tools/test-a733-imx214-cold.py']
files += list((repo/'tools/camera-control-host-fixture').rglob('*.h'))
record=dict(result='CAMERA_V192_HOST_AND_ARM_OBJECTS_PASSED_NOT_HARDWARE',files=records,
    source_sha256={str(f.relative_to(repo)):sha(f) for f in files},
    logs={f.name:sha(f) for f in a.output.glob('*.log')},
    full_firmware_linked=False,sdk_modified=False,hardware_verified=False,
    sensor_transfers=False,camera_capture=False,power_live_measurement=False)
(a.output/'summary.json').write_text(json.dumps(record,indent=2)+'\n')
if a.evidence:
    a.evidence.mkdir()
    for f in a.output.glob('*.log'):shutil.copyfile(f,a.evidence/f.name)
    record['files']=[{k:v for k,v in f.items() if k!='command'} for f in records]
    (a.evidence/'summary.json').write_text(json.dumps(record,indent=2)+'\n')
print(record['result'],[(s['source'],s['maximum_stack_frame']) for s in records])
