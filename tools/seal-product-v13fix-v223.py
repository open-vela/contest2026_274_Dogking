#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Versioned sealer: keep all prior checks and preserve v221 bytes too."""
from pathlib import Path
import json,hashlib
p=Path(__file__).with_name('seal-product-v12fix-v221.py');s=p.read_text()
s=s.replace('v221','v223').replace('V221','V223')
s=s.replace('v12fix','v13fix').replace('v1.2fix','v1.3fix')
s=s.replace('v1.3fix-v223-preview.img','v1.3fix-v223-candidate.img')
s=s.replace('desktop pet v1.3fix PREVIEW (v223;','desktop pet v1.3fix CANDIDATE (v223;')
s=s.replace('voice-service-v1.3fix-preview(v223)','voice-service-v1.3fix(v223)')
s=s.replace("len(voice['tests'])==28","len(voice['tests'])==31").replace('software_gates=28','software_gates=31')
# MCU diagnostic and regression keep their true implementation revision.
s=s.replace("b'mcu-v223:'","b'mcu-v221:'").replace("/'mcu-v223/summary.json'","/'mcu-v221/summary.json'")
s=s.replace("b'mcu-v221:',","b'gate-end-v221fix1:',b'mcu-v221:',")
s=s.replace("'preview-profile-v220','mcu-v223'","'preview-profile-v220','mcu-v221'")
needle=" 'openvela-a733-cubie-a7z-sd-v1.2-v220-preview.img':"
assert s.count(needle)==1
s=s.replace(needle," 'openvela-a733-cubie-a7z-sd-v1.2fix-v221-preview.img':'c07d26842cc0ddaa4a1e16b957c2c06297c94980c1af9c92bd6a303f5d8bac2c',\n"+needle)
exec(compile(s,str(p),'exec'))
import shutil
for source,target in ((a.voice_report/'voicefix/endpoint.json','old-new-endpoint.json'),
 (a.voice_report/'voicefix/provider.log','duration-boundaries.log'),
 (a.voice_report/'real-acoustic/summary.json','real-acoustic-summary.json'),
 (a.voice_report/'real-acoustic/run.log','real-acoustic.log'),
 (a.voice_report/'voicefix-arm/summary.json','voicefix-arm.json')):
    shutil.copyfile(source,evidence/target)
result.update(energy_gate_fixed_candidate=True,buffered_seconds=10,streaming_seconds=60,
 acoustic_phone_chunk=24,native_English=False,Latin_letter_spelling=True,
 MiMo_integration_added=False,latest_stable='v1.1/v219')
(evidence/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
