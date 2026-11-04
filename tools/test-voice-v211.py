#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Persist voice regression evidence; CPU/mocked NPU != board acceptance."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,time
r=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__)
p.add_argument('new_output',type=Path);p.add_argument('--build',type=Path,required=True)
a=p.parse_args();out=a.new_output.resolve()
assert out.is_relative_to(r/'out');out.mkdir()
rel=lambda n:str((out/n).relative_to(r))
tests=[
 ('q8-workspace',['python3','tools/test-q8-workspace-v211.py']),
 ('asr-tail',['python3','tools/test-asr-tail-v211.py']),
 ('session',['bash','tools/test-voice-session-v190.sh',rel('session')]),
 ('asr',['python3','tools/npu-voice/test-general-asr-v184.py',rel('asr')]),
 ('tts',['python3','tools/npu-voice/test-tts-engine-v185.py',rel('tts')]),
 ('stream',['bash','tools/build-voice-stream-v181.sh',rel('stream')]),
 ('audio',['bash','tools/test-aipet-audio.sh']),
 ('llm-affinity',['python3','tools/test-llm-affinity-v210.py']),
 ('llm-contract',['bash','tools/test-aipet-llm-v178.sh']),
 ('frontend',['bash','tools/test-frontend-audit.sh','out/a733-kws-wav-v161-r2/voice/kws/wav','out/a733-openvela-kws-v158-r2/voice/kws']),
 ('capture',['python3','tools/test-live-capture-v182.py']),
 ('tensor',['bash','tools/test-a733-tensor.sh']),
 ('tensor-arm',['python3','tools/test-tensor-crc-arm-v210.py']),
 ('voice-arm',['python3','tools/check-a733-voice-arm-v181.py',rel('voice-arm')])]
env=dict(os.environ,A733_VOICE_ABI_BUILD=str(a.build.resolve()),A733_GENERAL_VOICE_ARM_CHECK='1')
records=[]
for name,cmd in tests:
    start=time.monotonic();log=out/(name+'.log')
    with log.open('x') as f:subprocess.run(cmd,cwd=r,env=env,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=900)
    records.append(dict(name=name,command=cmd,seconds=time.monotonic()-start,sha256=hashlib.sha256(log.read_bytes()).hexdigest()))
    print('PASS '+name,flush=True)
(out/'summary.json').write_text(json.dumps(dict(result='V211_VOICE_HOST_ARM_PASSED_NOT_BOARD',tests=records,
  hardware_executed=False,physical_audio_onset_measured=False,latency_3_to_5_seconds=False),indent=2)+'\n')
