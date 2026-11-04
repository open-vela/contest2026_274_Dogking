#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Preserve v223 bytes; all inherited integrity and hardware-boundary gates."""
from pathlib import Path
import json,shutil
p=Path(__file__).with_name('seal-product-v13fix-v223.py');s=p.read_text()
s=s.replace('v223','v224').replace('V223','V224').replace('v13fix','v13fix2').replace('v1.3fix','v1.3fix2')
s=s.replace("==31","==33").replace("software_gates=31","software_gates=33")
insert='''s=s.replace("old_images={", "old_images={\\n 'openvela-a733-cubie-a7z-sd-v1.3fix-v223-candidate.img':'43380317ae7958d0d2eb12e61984603d411b05d478fbfa1db8d7ad20b522bb31',")
s=s.replace("b'gate-end-v221fix1:',", "b'gate-end-v224:',b'resident-ASR-discard-no-final-NPU',b'gate-end-v221fix1:',")
'''
needle="exec(compile(s,str(p),'exec'))";assert s.count(needle)==1
s=s.replace(needle,insert+needle)
exec(compile(s,str(p),'exec'))
for source,target in ((a.voice_report/'temporal-endpoint.log','temporal-endpoint.log'),
 (a.voice_report/'asr-native-discard-replay/summary.json','asr-native-discard-replay.json'),
 (a.voice_report/'asr-native-discard-replay/run.log','asr-native-discard-replay.log')):
    shutil.copyfile(source,evidence/target)
result.update(software_gates=33,temporal_continuation_ms=80,overlong_final_decode_skipped=True,
              hardware_hang_root_cause_proven=False)
(evidence/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
