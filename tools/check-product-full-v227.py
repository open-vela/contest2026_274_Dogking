#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Preserve every ABI/ISA/camera/ELF gate; exact two new application headers."""
from pathlib import Path
import subprocess,sys
root=Path(__file__).resolve().parents[1]
frozen=sys.argv[sys.argv.index('--firmware-commit')+1]
assert subprocess.run(['git','-c','core.autocrlf=true','diff','--quiet',frozen,'--','board/a733-cubie-a7z/openvela-overlay'],cwd=root).returncode==0
p=Path(__file__).with_name('check-product-full-v225.py');s=p.read_text()
needle="extra=('apps/system/aipetllm/aipetllm_prefill.inc'"
assert needle in s
s=s.replace(needle,"extra=('apps/system/aipet/cloud_persona.hxx','apps/system/aipet/cloud_prefetch.hxx','apps/system/aipetllm/aipetllm_prefill.inc'",1)
s=s.replace("exec(compile(s,str(p),'exec'))","s=s.replace('paths=316','paths=319')\nexec(compile(s,str(p),'exec'))",1)
s=s.replace('V225_VOICE_CAMERA_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED','V227_VOICE_CAMERA_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED')
s=s.replace('desktop pet v2.0pre CANDIDATE (v225;','desktop pet v3.0pre CANDIDATE (v227;').replace('voice-service-v2.0pre(v225)','voice-service-v3.0pre(v227)')
exec(compile(s,str(p),'exec'))
for label in (b'cloud-recovery-v226:',b'cloud-wss-v227:',b'wifi-recovery-v226:',b'cloud-xfyun-v227:',
              b'cloud-prefetch-v227:',b'product-model-action-v227:',b'cloud-llm-v227:'):
    assert label in binary,label
report.update(spoken_numeric_steps_scale=20,max_user_steps=512,tts_prefetch_slots=2,
 model_actions_default=False,max_model_actions_per_turn=1,SDK_restored_paths=319,
 latency_accepted=False,board_tested=False)
f.write_text(json.dumps(report,indent=2)+'\n')
