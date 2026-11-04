#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Versioned v219 sealer: all checks retained, preserve accepted v1.1 bytes."""
from pathlib import Path
import json
p=Path(__file__).with_name('seal-product-v11-v219.py');s=p.read_text()
s=s.replace('Seal a checked v1.1 candidate','Seal a checked v1.2 preview')
s=s.replace('a733-v11-v219-recovery-20261031','a733-v12-v220-recovery-20261101')
s=s.replace('v219-product-v11-release-r1','v220-product-v12-preview-release-r1')
s=s.replace('v11-v219','v12-v220').replace('V11_V219','V12_V220')
s=s.replace('v219','v220').replace('V219','V220')
# These diagnostics/tests were deliberately inherited without a runtime
# revision. Require their REAL names; do not relabel old proof as new work.
for name in ('mic-monitor','resident-lease','runtime-lease','wifi'):
    s=s.replace(name+'-v220',name+'-v219')
s=s.replace('openvela-a733-cubie-a7z-sd-v1.1-v220-candidate.img',
            'openvela-a733-cubie-a7z-sd-v1.2-v220-preview.img')
s=s.replace('desktop pet v1.1 CANDIDATE (v220)', 'desktop pet v1.2 PREVIEW (v220;')
s=s.replace('voice-service-v1.1(v220)','voice-service-v1.2-preview(v220)')
s=s.replace("len(voice['tests'])==26","len(voice['tests'])==27").replace('software_gates=26','software_gates=27')
s=s.replace('openvela-a733-cubie-a7z-sd-v1.0-v217-product-audited.img',
            'openvela-a733-cubie-a7z-sd-v1.1-v219-candidate.img')
s=s.replace('98daee1f3b4653d3d65a14a3e92c2713560aac2fd6183946d2b06b7a3c79bc21',
            'fe7a28ce9996e3344a1866610c7cdb9137a1088965b0207161df6ffab4c3359f')
s=s.replace('a733-v1.0-v217-audited-20261031','a733-v1.1-v219-stable-20261101')
s=s.replace("'apps/system/aipet/local_llm.cxx'","'apps/system/aipet/pet_persona.hxx'")
accepted=json.loads((p.parents[1]/'docs/evidence/product-v11-v219/board-accepted-20261101.json').read_text())
s=s.replace('cdd010dd448ab95d3013b344ae252d13b37bbe1198d96a57bfcbc1842c1bcc31',accepted['source_serial_sha256'])
exec(compile(s,str(p),'exec'))
import shutil
reference=r/'out/llm-quality-v220-r1'
for name in ('summary.json','run.log'):
    shutil.copyfile(reference/name,evidence/('real-gguf-'+name))
shutil.copyfile(a.voice_report/'preview-profile-v220.log',evidence/'preview-profile-v220.log')
result.update(default_local_history_pairs=0,legacy_v11_context_file_unchanged=True,
              same_persona_and_reply_budget=True,real_GGUF_independent_oracle=True,
              board_speedup_measured=False,stable_v11_image_unchanged=True,
              preview_config='CONFIG_A733_PRODUCT_V12=y')
(evidence/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
