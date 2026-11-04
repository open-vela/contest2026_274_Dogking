#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import argparse,hashlib,json,shlex,shutil,subprocess,sys
from pathlib import Path
repo=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--build',required=True,type=Path)
p.add_argument('--output',required=True,type=Path);p.add_argument('--evidence',type=Path)
a=p.parse_args();a.output.mkdir()
subprocess.run([sys.executable,str(repo/'tools/check-a733-camera-v196.py'),
    '--build',str(a.build),'--output',str(a.output/'base')],check=True,timeout=720)
scripts=['test-a733-csi-capture.py','test-a733-camera-frame.py',
         'test-a733-camera-export.py','test-a733-display-image.py']
for script in scripts:
    with (a.output/(script+'.log')).open('x') as f:
        subprocess.run([sys.executable,str(repo/'tools'/script)],stdout=f,
                       stderr=subprocess.STDOUT,check=True,timeout=120)
with (a.output/'handoff-v197.log').open('x') as f:
    subprocess.run([sys.executable,str(repo/'tools/test-a733-camera-handoff-v194.py'),'197'],
                   stdout=f,stderr=subprocess.STDOUT,check=True,timeout=90)
chip=repo/'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733'
apps=repo/'board/a733-cubie-a7z/openvela-overlay/apps'
base192=a.output/'base/base/base/base/base/summary.json'
template=json.loads(base192.read_text())['files'][0]['command']
db=json.loads((a.build/'compile_commands.json').read_text())
display=next(e for e in db if e['file'].endswith('/a733display_main.c'))
command=shlex.split(display['command']);display_template=[];i=0
while i<len(command):
    if command[i] in ('-MF','-MT','-MQ'):i+=2
    elif command[i] in ('-MD','-MMD'):i+=1
    else:display_template.append(command[i]);i+=1
records=[]
for src,original in ((chip/'a733_csi_capture.c',template),
                     (apps/'system/a733display/a733display_main.c',display_template)):
    obj=a.output/(src.stem+'.o');cmd=list(original)
    cmd[cmd.index('-c')+1]=str(src.resolve());cmd[cmd.index('-o')+1]=str(obj.resolve())
    cmd+=['-fstack-usage','-Werror=frame-larger-than=4096','-Wframe-larger-than=4096']
    if src.name=='a733display_main.c':
        cmd+=['-I'+str(apps/'include'),'-include',str(apps/'include/system/a733_vision.h'),'-include',str(chip/'include/a733_peripherals.h'),
          '-include',str(repo/'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/boards/a733/cubie-a7z/include/board.h')]
    with (a.output/(src.stem+'.log')).open('x') as f:
        subprocess.run(cmd,cwd=display['directory'] if src.name=='a733display_main.c' else repo,
                       stdout=f,stderr=subprocess.STDOUT,check=True,timeout=120)
    frames=[int(line.rsplit('\t',2)[1]) for line in obj.with_suffix('.su').read_text().splitlines()]
    assert max(frames)<=4096
    records.append(dict(source=str(src.relative_to(repo)),object_sha256=hashlib.sha256(obj.read_bytes()).hexdigest(),maximum_stack_frame=max(frames)))
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
base=json.loads((a.output/'base/summary.json').read_text())
files=list(base['source_sha256'])+[str((chip/n).relative_to(repo)) for n in ('a733_csi_capture.c','a733_csi_capture.h')]
files += [r['source'] for r in records]+['tools/check-a733-camera-v197.py','tools/build-a733-camera-v197.sh',
  'tools/a733_camera_test_build.py','tools/test-a733-csi-capture.c','tools/test-a733-camera-frame.c',
  'tools/test-a733-camera-export.c','tools/Get-A733FileSpanDigest.ps1',
  'tools/package-a733-boot-v188.py']+['tools/'+s for s in scripts]
refs=['drivers/vin/vin-csi/parser_reg_i.h','drivers/vin/vin-csi/parser_reg.c',
 'drivers/vin/vin-csi/sunxi_csi.c','drivers/vin/vin-video/dma140/dma140_reg_i.h',
 'drivers/vin/vin-video/dma140/dma140_reg.c','drivers/vin/vin-video/vin_video.c',
 'drivers/vin/top_reg_i.h','drivers/vin/top_reg.c','drivers/vin/vin.c',
 'drivers/vin/vin-isp/sunxi_isp.c','drivers/vin/vin-vipp/sunxi_scaler.c',
 'drivers/vin/platform/sun60iw2_vin_cfg.h','drivers/clk/sunxi-ng/ccu-sun60iw2.c',
 'drivers/iommu/sunxi-iommu-v2.c','configs/linux-6.1/sun60iw2p1.dtsi']
result=dict(result='CAMERA_V197_GUARDED_RAW_FRAME_HOST_AND_ARM_PASSED_NOT_HARDWARE',
 source_sha256={f:sha(repo/f) for f in files},base_summary_sha256=sha(a.output/'base/summary.json'),
 logs={f.name:sha(f) for f in a.output.glob('*.log')},objects=records,
 primary_reference_sha256={'bsp-product-aiot-stable/'+f:sha(repo.parent/'bsp-product-aiot-stable'/f) for f in refs},
 hardware_verified=False,real_frame_captured_on_board=False)
(a.output/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
if a.evidence:
    a.evidence.mkdir()
    for f in a.output.glob('*.log'):shutil.copyfile(f,a.evidence/f.name)
    shutil.copyfile(a.output/'summary.json',a.evidence/'summary.json')
print(result['result'],records)
