#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Actual product ARM flags/stack audit, including the v221 Agent tool unit."""
from pathlib import Path
import json
p=Path(__file__).with_name('check-a733-product-arm-v1.py')
s=p.read_text()
old="'mcu_protocol.cxx','mcu_link.cxx')"
assert s.count(old)==1
s=s.replace(old,"'mcu_protocol.cxx','mcu_link.cxx','mcu_agent_tool.cxx')")
state=dict(__file__=str(p),__name__='__main__')
exec(compile(s,str(p),'exec'),state)
out=state['out'];build=state['build']
assert 'CONFIG_A733_PRODUCT_V12=y' in (build/'.config').read_text()
assert b'mcu-v221:' in (out/'mcu_link.cxx.o').read_bytes()
assert b'v1.2fix PREVIEW (v221;' in (out/'product_runtime.cxx.o').read_bytes()
assert b'voice-service-v1.2fix-preview(v221)' in (out/'audio_app.cxx.o').read_bytes()
report=json.loads((out/'summary.json').read_text())
report.update(result='V221_NUTTX_ARM_COMPILE_PASSED_NOT_FULL_LINK_OR_BOARD',
              preview_config=True,SDK_sources_modified=False)
(out/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
print(report['result'])
