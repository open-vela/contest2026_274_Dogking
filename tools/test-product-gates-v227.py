#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""All 40 inherited gates plus actual v227 cloud policy/prefetch/action units."""
from pathlib import Path
import runpy,subprocess,json,hashlib,time
p=Path(__file__).with_name('test-product-gates-v226.py')
state=runpy.run_path(str(p),run_name='__main__');r=state['r'];out=state['out']
report=json.loads((out/'summary.json').read_text());assert len(report['tests'])==40
cmd=['python3','tools/test-product-v227.py'];log=out/'cloud-policy-prefetch-v227.log';start=time.monotonic()
with log.open('x') as f:subprocess.run(cmd,cwd=r,check=True,timeout=180,stdout=f,stderr=subprocess.STDOUT)
report['tests'].append(dict(name='cloud-policy-prefetch-v227',command=cmd,seconds=time.monotonic()-start,sha256=hashlib.sha256(log.read_bytes()).hexdigest()))
report.update(result='V227_VOICE_HOST_ARM_PASSED_NOT_BOARD',software_gates=41,
 product='desktop-pet-v3.0pre-v227-candidate',spoken_numeric_steps_scale=20,UI_steps_scale=20,
 max_user_steps=512,max_wire_pulses=10240,model_actions_default=False,max_model_actions_per_turn=1,
 desktop_exact_human_command_consent=False,desktop_rule_commands_exact=True,
 model_motion_requires_opt_in=True,model_motion_requires_current_invitation=True,
 tts_prefetch_slots=2,deepseek_flash_thinking='explicitly-disabled-official-host-only',
 hardware_executed=False,cloud_account_calls=False,latency_accepted=False)
(out/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS v227: 41 software gates; NOT board/cloud account or latency acceptance',flush=True)
