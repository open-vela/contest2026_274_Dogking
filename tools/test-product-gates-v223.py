#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""All28 product gates plus endpoint/provider, real acoustic and final ARM checks."""
from pathlib import Path
import subprocess,json,hashlib,time,os
p=Path(__file__).with_name('test-product-gates-v221.py');s=p.read_text()
s=s.replace('desktop pet v1.2fix PREVIEW (v221;','desktop pet v1.3fix CANDIDATE (v223;')
s=s.replace('V221_VOICE_HOST_ARM_PASSED_NOT_BOARD','V223_VOICE_HOST_ARM_PASSED_NOT_BOARD')
s=s.replace('desktop-pet-v1.2fix-v221-preview','desktop-pet-v1.3fix-v223-candidate')
state=dict(__file__=str(p),__name__='__main__');exec(compile(s,str(p),'exec'),state)
r=state['r'];out=state['out'];build=state['state']['build']
env=dict(os.environ,A733_VOICE_ABI_BUILD=str(build.resolve()),A733_GENERAL_VOICE_ARM_CHECK='1')
report=json.loads((out/'summary.json').read_text());assert len(report['tests'])==28
for name,cmd in [('voicefix',['python3','tools/test-v223-voicefix.py','--output',str(out/'voicefix')]),
 ('real-acoustic',['python3','tools/npu-voice/test-tts-engine-v185.py',str(out/'real-acoustic')]),
 ('voicefix-arm',['python3','tools/check-voice-fix1-arm.py',str(out/'voicefix-arm')])]:
    log=out/(name+'.log');start=time.monotonic()
    with log.open('x') as f:subprocess.run(cmd,cwd=r,env=env,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=900)
    report['tests'].append(dict(name=name,seconds=time.monotonic()-start,sha256=hashlib.sha256(log.read_bytes()).hexdigest()))
    print('PASS '+name,flush=True)
report.update(result='V223_VOICE_HOST_ARM_PASSED_NOT_BOARD',product='desktop-pet-v1.3fix-v223-candidate',
              native_English=False,Latin_letter_spelling=True,cloud_account_calls=False)
(out/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS v223:31 gates; NOT board acceptance',flush=True)
