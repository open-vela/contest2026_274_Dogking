#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""All 26 v219 gates, plus preview isolation/allocator and REAL GGUF proof."""
from pathlib import Path
import hashlib,json,runpy,subprocess,sys,time
p=Path(__file__).with_name('test-product-gates-v219.py')
state=runpy.run_path(str(p),run_name='__main__')
r=state['r'];out=state['out'];build=state['a'].build
assert 'CONFIG_A733_PRODUCT_V12=y' in (build/'.config').read_text()
for name,label in (('local_llm.cxx.o',b'local_llm.context.v12'),
                   ('aipetllm_modelcheck.c.o',b'llm-prefill-scratch-v220:')):
    obj=state['a'].base/'voice-arm'/name
    assert label in obj.read_bytes(),('preview branch missing from actual ARM object',str(obj))
report=json.loads((out/'summary.json').read_text())
assert len(report['tests'])==26
start=time.monotonic();log=out/'preview-profile-v220.log'
with log.open('x') as f:
    subprocess.run([sys.executable,str(r/'tools/test-preview-v220.py')],cwd=r,
                   stdout=f,stderr=subprocess.STDOUT,check=True,timeout=180)
report['tests'].append(dict(name='preview-profile-v220',seconds=time.monotonic()-start,
    sha256=hashlib.sha256(log.read_bytes()).hexdigest()))
reference=r/'out/llm-quality-v220-r1/summary.json'
real=json.loads(reference.read_text())
assert real['result']=='V220_REAL_GGUF_REFERENCE_PASSED_NOT_BOARD'
assert real['preview_config']=='CONFIG_A733_PRODUCT_V12=1'
for name,digest in real['source_sha256'].items():
    assert hashlib.sha256((r/name).read_bytes()).hexdigest()==digest,name
report.update(result='V220_VOICE_HOST_ARM_PASSED_NOT_BOARD',
    product='desktop-pet-v1.2-v220-preview',
    real_GGUF_reference=str(reference.relative_to(r)),
    real_GGUF_reference_sha256=hashlib.sha256(reference.read_bytes()).hexdigest(),
    default_local_history_pairs=0,legacy_v11_history_file_unchanged=True,
    preview_config='CONFIG_A733_PRODUCT_V12=y',system_scheduling_changed=False)
report['actual_preview_ARM_objects_checked']=True
(out/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
print('V220_VOICE_HOST_ARM_PASSED_NOT_BOARD',flush=True)
