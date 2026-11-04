#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Local v216 source/bundle/references; v215 and user's untracked docs retained."""
from pathlib import Path
p=Path(__file__).with_name('checkpoint-voice-v214.py');s=p.read_text()
s=s.replace('v214','v216').replace('V214_','V216_')
s=s.replace('voice-regression-v216-r6','voice-regression-v216-r2')
s=s.replace('voice-resident-candidate.img','voice-quality-candidate.img')
s=s.replace('voice-resident-recovery','voice-quality-recovery')
marker="references += [p for p in (r/'out/ort-static-v185-r2')"
assert s.count(marker)==1
s=s.replace(marker,"references += [r.parent/'openvela-a733-cubie-a7z-sd-v215-voice-load-ufs-candidate.img',\n r/'out/checkpoints/a733-v215-voice-load-ufs-recovery-20261030/manifest.json']\nreferences += [p for p in (r/'out/llm-quality-v216-r4').rglob('*') if p.is_file()]\n"+marker)
exec(compile(s,str(p),'exec'))
