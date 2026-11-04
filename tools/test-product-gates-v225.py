#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""All inherited voice/product gates + actual v225 command/boot behavior."""
from pathlib import Path
import runpy,subprocess,json,hashlib,time,os
p=Path(__file__).with_name('test-product-gates-v219.py')
state=runpy.run_path(str(p),run_name='__main__')
r=state['r'];out=state['out'];build=state['a'].build
assert 'CONFIG_A733_PRODUCT_V12=y' in (build/'.config').read_text()
for name,label in (('local_llm.cxx.o',b'local_llm.context.v12'),
                   ('aipetllm_modelcheck.c.o',b'llm-prefill-scratch-v220:')):
    assert label in (state['a'].base/'voice-arm'/name).read_bytes(),name
report=json.loads((out/'summary.json').read_text());assert len(report['tests'])==26
env=dict(os.environ,A733_VOICE_ABI_BUILD=str(build.resolve()),A733_GENERAL_VOICE_ARM_CHECK='1')
for name,cmd in [
 ('preview-profile-v220',['python3','tools/test-preview-v220.py']),
 ('mcu-v221',['python3','tools/test-mcu-v221.py','--output',str(out/'mcu-v221')]),
 ('voicefix',['python3','tools/test-v223-voicefix.py','--output',str(out/'voicefix')]),
 ('real-acoustic',['python3','tools/npu-voice/test-tts-engine-v185.py',str(out/'real-acoustic')]),
 ('voicefix-arm',['python3','tools/check-voice-v224-arm.py',str(out/'voicefix-arm')]),
 ('temporal-endpoint',['python3','tools/test-v224-endpoint-regression.py']),
 ('asr-native-discard-replay',['python3','tools/npu-voice/test-general-asr-v184.py',str(out/'asr-native-discard-replay')]),
 ('v225-command-route',['python3','tools/test-product-v225.py']),
 ('v225-boot-policy',['python3','tools/test-boot-policy-v225.py']),
 ('v225-boot-worker',['python3','tools/test-product-boot-v225.py'])]:
    log=out/(name+'.log');start=time.monotonic()
    with log.open('x') as f:subprocess.run(cmd,cwd=r,env=env,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=900)
    report['tests'].append(dict(name=name,command=cmd,seconds=time.monotonic()-start,sha256=hashlib.sha256(log.read_bytes()).hexdigest()))
    print('PASS '+name,flush=True)
reference=r/'out/llm-quality-v220-r1/summary.json';real=json.loads(reference.read_text())
assert real['result']=='V220_REAL_GGUF_REFERENCE_PASSED_NOT_BOARD'
assert real['preview_config']=='CONFIG_A733_PRODUCT_V12=1'
inherited={};application_changed={}
for name,digest in real['source_sha256'].items():
    actual=hashlib.sha256((r/name).read_bytes()).hexdigest()
    if name.endswith('/product_runtime.cxx'):
        application_changed[name]=dict(reference=digest,current=actual,
          reason='v225 real settings, mute and opt-in boot integration; covered by runtime/service/boot tests')
    else:
        assert actual==digest,name
        inherited[name]=actual
assert len(report['tests'])==36
report.update(result='V225_VOICE_HOST_ARM_PASSED_NOT_BOARD',software_gates=36,
 product='desktop-pet-v2.0pre-v225-candidate',default_local_history_pairs=0,
 real_GGUF_reference=str(reference.relative_to(r)),real_GGUF_reference_sha256=hashlib.sha256(reference.read_bytes()).hexdigest(),
 inherited_LLM_math_sources=inherited,application_delta=application_changed,
 boot_listening_default=False,desktop_exact_human_command_consent=True,
 spoken_numeric_steps_scale=10,voice_move_speed=2,voice_move_Hz=300,
 hardware_executed=False,board_tested=False,cloud_account_calls=False,system_scheduling_changed=False)
(out/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS v225:36 software gates; NOT board acceptance',flush=True)
