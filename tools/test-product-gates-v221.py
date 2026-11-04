#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Rerun all 27 inherited gates plus pinned-upstream STM32 regression."""
from pathlib import Path
import json,subprocess,sys,time,hashlib
p=Path(__file__).with_name('test-product-gates-v220.py');s=p.read_text()
old="    assert hashlib.sha256((r/name).read_bytes()).hexdigest()==digest,name"
assert s.count(old)==1
# This source is included by the old oracle harness. Its sole v221 change is
# the product banner; verify the entire file after reversing ONLY that banner.
s=s.replace(old,"""    data=(r/name).read_bytes()
    if name.endswith('/product_runtime.cxx'):
        assert data.count(b'desktop pet v1.2fix PREVIEW (v221;')==1
        data=data.replace(b'desktop pet v1.2fix PREVIEW (v221;',b'desktop pet v1.2 PREVIEW (v220;')
    assert hashlib.sha256(data).hexdigest()==digest,name""")
s=s.replace('V220_VOICE_HOST_ARM_PASSED_NOT_BOARD','V221_VOICE_HOST_ARM_PASSED_NOT_BOARD').replace('desktop-pet-v1.2-v220-preview','desktop-pet-v1.2fix-v221-preview')
state=dict(__file__=str(p),__name__='__main__');exec(compile(s,str(p),'exec'),state)
r=state['r'];out=state['out'];log=out/'mcu-v221.log';start=time.monotonic()
with log.open('x') as f:
    subprocess.run([sys.executable,str(r/'tools/test-mcu-v221.py'),'--output',str(out/'mcu-v221')],cwd=r,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=900)
report=json.loads((out/'summary.json').read_text());assert len(report['tests'])==27
report['tests'].append(dict(name='mcu-v221',seconds=time.monotonic()-start,sha256=hashlib.sha256(log.read_bytes()).hexdigest()))
report.update(LLM_oracle_inherited_v220_exact_math=True,product_banner_only_difference_verified=True)
(out/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS v221: 28 software gates; no board acceptance',flush=True)
