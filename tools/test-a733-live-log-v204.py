#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Synthetic log parser fixtures, NOT device execution/acceptance."""
from pathlib import Path
import runpy

audit=runpy.run_path(str(Path(__file__).with_name('audit-a733-live-board-v204.py')))['audit']
accept='live: accepted seconds=10 detect-every=1 (0=preview), RAM pipeline\n'
frame='live frame=1 input=12345678 boxes=1 inferred=1 capture=100 prepare=200 npu=300 display=400 us\n'
npu='live npu seq=1 outputs=00000001/00000002/00000003/00000004/00000005/00000006 fallback=0\n'
det='live det seq=1 class=0 score=900 box=10,20,100,200\n'
end='camera-live-v204: active=0 stop=1 stage=complete error=0 cleanup=0 frames=1 inference=1 elapsed-ms=1000 fps=1.000 input-crc32=12345678\n'
valid=accept+frame+npu+det+end
r=audit(valid);assert r['completed_frames']==1 and r['detections']==1 and not r['hardware_verified']
mutations=[valid.replace(npu,''),valid.replace(det,''),valid.replace('seq=1 outputs','seq=2 outputs'),
 valid.replace('score=900','score=1001'),valid.replace('box=10,20,100,200','box=100,20,10,200'),
 valid.replace('cleanup=0','cleanup=-16'),valid.replace('stage=complete','stage=failed'),
 valid.replace('fps=1.000','fps=2.000'),valid.replace('input-crc32=12345678','input-crc32=12345679'),
 valid.replace('frames=1 inference=1','frames=2 inference=1'),valid.replace(end,''),
 valid.replace('inferred=1','inferred=0'),
 valid.replace(npu,frame.replace('frame=1','frame=2')+npu)]
for bad in mutations:
    try:audit(bad)
    except ValueError:pass
    else:raise AssertionError('Accepted invalid/incomplete synthetic log')
preview=accept.replace('detect-every=1','detect-every=0')+frame.replace('boxes=1 inferred=1','boxes=0 inferred=0')+end.replace('frames=1 inference=1','frames=19 inference=0').replace('fps=1.000','fps=19.000')
r=audit(preview);assert r['completed_frames']==19 and r['logged_frames']==1 and not r['completed_inferences']
zero=valid.replace('boxes=1','boxes=0').replace(det,'');assert audit(zero)['zero_detection_inferences']==1
assert audit(valid+preview)['completed_frames']==19
assert audit(valid+preview,0)['completed_frames']==1
print('PASS synthetic live log consistency fixtures; NOT board/FPS/visual acceptance')
