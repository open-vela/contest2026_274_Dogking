#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile live sources against real SDK headers/ARM flags, not device tests."""
from pathlib import Path
import argparse,hashlib,json,shlex,subprocess
repo=Path(__file__).resolve().parents[1];overlay=repo/'board/a733-cubie-a7z/openvela-overlay'
p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
p.add_argument('--precheck',action='store_true');p.add_argument('--perf',action='store_true');a=p.parse_args();a.output.mkdir()
db=json.loads((a.build/'compile_commands.json').read_text());results=[]
if not a.precheck:
    config=(a.build/'.config').read_text()
    assert 'CONFIG_A733_CAMERA_LIVE_TEST=y' in config and 'CONFIG_FS_TMPFS=y' in config
    if a.perf:assert 'CONFIG_A733_CAMERA_PERF_TEST=y' in config
files=['apps/system/a733camera/a733camera_main.c','apps/system/a733camera/a733_camera_image.c',
       'apps/system/a733camera/a733_camera_live.c','apps/system/a733display/a733display_main.c',
       'vendor/allwinnertech/chips/a733/a733_camera_control.c',
       'vendor/allwinnertech/chips/a733/a733_npu_runtime.c']
if a.perf:files+=['vendor/allwinnertech/chips/a733/a733_csi_capture.c','vendor/allwinnertech/chips/a733/a733_imx214_cold.c']
for file in files:
    template=file if not file.endswith('a733_camera_live.c') else 'apps/system/a733camera/a733camera_main.c'
    item=next(x for x in db if x['file'].endswith('/'+template))
    args=shlex.split(item['command']);out=[];skip=False
    for token in args:
        if skip:skip=False;continue
        if token in ('-o','-MF','-MT','-MQ'):skip=True;continue
        if token in ('-MD','-MMD','-MP') or token==item['file']:continue
        out.append(token)
    target=a.output/(Path(file).stem+'.o')
    cmd=[out[0],'-I'+str(overlay/'apps/include'),'-I'+str(overlay/'nuttx/include'),
         '-I'+str(overlay/'vendor/allwinnertech/chips/a733/include'),*out[1:]]
    if 'a733display' in file:
        cmd+=['-include',str(overlay/'vendor/allwinnertech/boards/a733/cubie-a7z/include/board.h'),
              '-include',str(overlay/'vendor/allwinnertech/chips/a733/include/a733_peripherals.h')]
    if a.precheck:cmd+=['-DCONFIG_A733_CAMERA_LIVE_TEST=1','-DCONFIG_FS_TMPFS=1']
    if a.perf:cmd+=['-DCONFIG_A733_CAMERA_PERF_TEST=1']
    cmd+=['-fstack-usage','-Wframe-larger-than=4096','-Werror=frame-larger-than=4096',str(overlay/file),'-o',str(target.resolve())]
    with (a.output/(target.stem+'.log')).open('x') as f:
        subprocess.run(cmd,cwd=item['directory'],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=60)
    su=target.with_suffix('.su');sizes=[int(x.split('\t')[1]) for x in su.read_text().splitlines()]
    assert sizes and max(sizes)<=4096
    results.append(dict(source=str((overlay/file).relative_to(repo)),sha256=hashlib.sha256((overlay/file).read_bytes()).hexdigest(),
      object_sha256=hashlib.sha256(target.read_bytes()).hexdigest(),maximum_stack=max(sizes),command=cmd))
prefix='V205' if a.perf else 'V204'
result=dict(result=prefix+('_ARM_PRECHECK_PASSED' if a.precheck else '_NATIVE_CONFIG_ARM_OBJECTS_PASSED'),files=results,hardware_verified=False)
(a.output/'summary.json').write_text(json.dumps(result,indent=2)+'\n');print(result['result'])
