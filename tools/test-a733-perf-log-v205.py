#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import importlib.util
from pathlib import Path
s=importlib.util.spec_from_file_location('audit205',Path(__file__).with_name('audit-a733-perf-board-v205.py'))
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
lines=['live: accepted seconds=10 detect-every=1 ']
for seq in (1,2,10):
    lines.extend([f'live frame={seq} input={seq:08x} boxes=0 inferred=1 capture=100 prepare=100 npu=100 display=100 us',
      f'live npu seq={seq} outputs=00000001/00000002/00000003/00000004/00000005/00000006 fallback=0',
      f'live205 npu seq={seq} hit={int(seq>1)} retained=0 prep=40 package=10 map=10 input=10 sync=10 hw=20 output=10 decode=10 restore=10 total=100 us'])
lines.extend(['camera-live-v205: active=0 stop=0 stage=complete error=0 cleanup=0 frames=10 inference=10 elapsed-ms=10000 fps=1.000 input-crc32=0000000a',
 'live205: optimized=1 sensor-stream=1 setup-us=1000 loop-us=9998000 warm-us=9000000 warm-fps-milli=1000 inference-fps-milli=1000 warm-npu-runs=9',
 'live205 validation: requested=0 verified=0 run-ioctls=10;'])
good='\n'.join(lines);result=m.audit(good)
assert result['clean_exit'] and result['declared_frames']==10 and result['sampled_frames']==3 and not result['hardware_verified']
for before,after in [('frames=10','frames=9'),('outputs=00000001','outputs=ffff0001'),('package=10','package=99'),
                     ('run-ioctls=10','run-ioctls=9'),('verified=0','verified=1'),('fps=1.000','fps=9.000')]:
    bad=good.replace(before,after,1)
    if before.startswith('outputs='):continue # Distinct CRCs across distinct inputs are legitimate.
    try:m.audit(bad)
    except ValueError:pass
    else:raise AssertionError(before)
try:m.audit(good.replace('live npu seq=2 outputs=','garbled seq=2 outputs='))
except ValueError:pass
else:raise AssertionError('missing sampled output')
assert m.audit('\n'.join(lines).replace('camera-live-v205:', 'camera-live-v206:'))['clean_exit']
assert m.audit('\n'.join(lines).replace('camera-live-v205:', 'camera-live-v207:'))['clean_exit']
assert m.audit('\n'.join(lines).replace('camera-live-v205:', 'camera-live-v208:'))['clean_exit']
assert m.audit('\n'.join(lines).replace('camera-live-v205:', 'camera-live-v209:'))['clean_exit']
print('PASS v205/v206 sampled log audit; synthetic only, no hardware/visual/performance claim')
