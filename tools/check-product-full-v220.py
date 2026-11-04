#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Keep all full-link/ABI/ISA/camera checks and require actual preview labels."""
from pathlib import Path
import json
p=Path(__file__).with_name('check-product-full-v219.py')
s=p.read_text()
assert s.count('extra=tuple(')==1
s=s.replace('extra=tuple(',
  "extra=('apps/system/aipetllm/aipetllm_prefill.inc',)+tuple(",1)
state=dict(__file__=str(p),__name__='__main__')
exec(compile(s,str(p),'exec'),state)
a=state['a'];r=state['r']
assert 'CONFIG_A733_PRODUCT_V12=y' in (a.build/'.config').read_text()
binary=(a.build/'nuttx.bin').read_bytes()
for label in (b'desktop pet v1.2 PREVIEW (v220;',b'voice-service-v1.2-preview(v220)',
              b'local_llm.context.v12',b'local-latency-v220:',b'llm-prefill-scratch-v220:',b'llm-latency-v220:'):
    assert label in binary,label
f=a.output/'summary.json';report=json.loads(f.read_text())
report.update(result='V220_VOICE_CAMERA_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED',
              preview_config=True,default_local_history_pairs=0,
              stable_tag='a733-v1.1-v219-stable-20261101')
f.write_text(json.dumps(report,indent=2)+'\n')
print(report['result'])
