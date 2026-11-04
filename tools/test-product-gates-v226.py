#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""All 36 inherited gates plus actual recovery, Wi-Fi health, TLS and providers."""
from pathlib import Path
import runpy,subprocess,json,hashlib,time
p=Path(__file__).with_name('test-product-gates-v225.py')
state=runpy.run_path(str(p),run_name='__main__')
r=state['r'];out=state['out'];report=json.loads((out/'summary.json').read_text())
assert len(report['tests'])==36
for name,cmd in [
 ('cloud-recovery-v226',['python3','tools/test-cloud-recovery-v226.py']),
 ('wifi-recovery-v226',['python3','tools/test-wifi-recovery-v226.py']),
 ('actual-wss-v226',['python3','tools/test-cloud-socket-v218.py','--mbedtls-build','out/cloud-voice-v218-mbedtls-host-r1','--output',str(out/'actual-wss')]),
 ('cloud-providers-v226',['python3','tools/test-cloud-voice-v218.py','--output',str(out/'cloud-providers')])]:
 log=out/(name+'.log');start=time.monotonic()
 with log.open('x') as f:subprocess.run(cmd,cwd=r,check=True,timeout=900,stdout=f,stderr=subprocess.STDOUT)
 report['tests'].append(dict(name=name,command=cmd,seconds=time.monotonic()-start,sha256=hashlib.sha256(log.read_bytes()).hexdigest()))
 print('PASS '+name,flush=True)
assert len(report['tests'])==40
report.update(result='V226_VOICE_HOST_ARM_PASSED_NOT_BOARD',software_gates=40,
 product='desktop-pet-v2.1pre-v226-candidate',recovery_attempts=3,local_hold_ms=120000,
 resident_cloud_mode='cloud-preferred-with-local-fallback',action_replay=False,
 played_audio_replay=False,rf_or_AP_qualified=False)
(out/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS v226:40 software gates; NOT new board/cloud/RF acceptance',flush=True)
