#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import argparse,hashlib,json,shlex,subprocess,sys
from pathlib import Path
repo=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--build',required=True,type=Path)
p.add_argument('--output',required=True,type=Path);p.add_argument('--evidence',type=Path)
a=p.parse_args();a.output.mkdir()
subprocess.run([sys.executable,str(repo/'tools/check-a733-camera-v197.py'),
    '--build',str(a.build),'--output',str(a.output/'base')],check=True,timeout=800)
for script,args in [('test-a733-vision-v198.py',[]),('test-a733-camera-handoff-v194.py',['198'])]:
    with (a.output/(script+'.log')).open('x') as f:
        subprocess.run([sys.executable,str(repo/'tools'/script),*args],stdout=f,
                       stderr=subprocess.STDOUT,check=True,timeout=120)
apps=repo/'board/a733-cubie-a7z/openvela-overlay/apps'
chip=repo/'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733'
src=apps/'system/a733npu/a733npu_main.c'
db=json.loads((a.build/'compile_commands.json').read_text())
entry=next(e for e in db if e['file'].endswith('/a733npu_main.c'))
tokens=shlex.split(entry['command']);cmd=[];i=0
while i<len(tokens):
    if tokens[i] in ('-MF','-MT','-MQ'):i+=2
    elif tokens[i] in ('-MD','-MMD'):i+=1
    else:cmd.append(tokens[i]);i+=1
obj=a.output/'a733npu_main.o'
cmd[cmd.index('-c')+1]=str(src.resolve());cmd[cmd.index('-o')+1]=str(obj.resolve())
cmd+=['-fstack-usage','-Werror=frame-larger-than=4096','-Wframe-larger-than=4096',
      '-I'+str(apps/'include'),'-include',str(apps/'include/system/a733_vision.h'),
      '-include',str(chip/'include/a733_peripherals.h')]
with (a.output/'a733npu_main.log').open('x') as f:
    subprocess.run(cmd,cwd=entry['directory'],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=120)
frames=[int(s.rsplit('\t',2)[1]) for s in obj.with_suffix('.su').read_text().splitlines()]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
base=json.loads((a.output/'base/summary.json').read_text())
files=list(base['source_sha256'])+[str(src.relative_to(repo)),str((apps/'include/system/a733_vision.h').relative_to(repo)),
  'tools/check-a733-camera-v198.py','tools/build-a733-camera-v198.sh','tools/test-a733-vision-v198.py']
result=dict(result='CAMERA_V198_SAME_INPUT_VISION_HOST_AND_ARM_PASSED_NOT_HARDWARE',
 source_sha256={n:sha(repo/n) for n in files},base_summary_sha256=sha(a.output/'base/summary.json'),
 logs={f.name:sha(f) for f in a.output.glob('*.log')},objects=[dict(source=str(src.relative_to(repo)),
 object_sha256=sha(obj),maximum_stack_frame=max(frames))],
 crc_primary_sha256=sha(repo.parent.parent/'quickly-openvela/nuttx/libs/libc/misc/lib_crc32.c'),
 hardware_verified=False,camera_frame_verified=False,real_camera_yolo_verified=False)
assert max(frames)<=4096
(a.output/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
if a.evidence:
    import shutil
    a.evidence.mkdir()
    for f in a.output.rglob('*'):
        if f.suffix in ('.log','.json','.su'):
            dst=a.evidence/f.relative_to(a.output);dst.parent.mkdir(exist_ok=True,parents=True);shutil.copyfile(f,dst)
print(result['result'],'producer-stack=',max(frames))
