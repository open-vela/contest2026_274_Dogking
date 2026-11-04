#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""All31 v223 gates plus v223/new endpoint comparison and native state replay."""
from pathlib import Path
import json,subprocess,time,hashlib
p=Path(__file__).with_name('test-product-gates-v223.py');s=p.read_text()
s=s.replace('v223','v224').replace('V223','V224').replace('v1.3fix','v1.3fix2')
# Keep old provider/duration checks, while adding the newer endpoint separately.
s=s.replace('tools/test-v224-voicefix.py','tools/test-v223-voicefix.py')
s=s.replace('tools/check-voice-fix1-arm.py','tools/check-voice-v224-arm.py')
state=dict(__file__=str(p),__name__='__main__');exec(compile(s,str(p),'exec'),state)
r=state['r'];out=state['out'];report=json.loads((out/'summary.json').read_text())
assert len(report['tests'])==31
for name,cmd in [('temporal-endpoint',['python3','tools/test-v224-endpoint-regression.py']),
 ('asr-native-discard-replay',['python3','tools/npu-voice/test-general-asr-v184.py',str(out/'asr-native-discard-replay')])]:
    log=out/(name+'.log');start=time.monotonic()
    with log.open('x') as f:subprocess.run(cmd,cwd=r,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=900)
    report['tests'].append(dict(name=name,seconds=time.monotonic()-start,sha256=hashlib.sha256(log.read_bytes()).hexdigest()))
    print('PASS '+name,flush=True)
report.update(result='V224_VOICE_HOST_ARM_PASSED_NOT_BOARD',software_gates=33,
              temporal_continuation_ms=80,overlong_final_decode_skipped=True,board_tested=False)
(out/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS v224:33 gates; NOT board acceptance',flush=True)
