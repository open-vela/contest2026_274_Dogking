#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Verify a local v214 recovery; preserve v213 and the untracked user summary."""
from pathlib import Path
import importlib.util,json
r=Path(__file__).resolve().parents[1]
references=[r.parent/'openvela-a733-cubie-a7z-sd-v214-voice-resident-candidate.img',
 r.parent/'openvela-a733-cubie-a7z-sd-v214-voice-resident-candidate.img.sha256',
 r.parent/'openvela-a733-cubie-a7z-sd-v213-voice-prepare-candidate.img',
 r.parent/'openvela-a733-cubie-a7z-sd-v210-voice-latency-candidate.img',
 r/'out/general-voice-assets-v185-r2/manifest.json',
 r/'out/checkpoints/a733-v213-voice-prepare-recovery-20261030/manifest.json']
references += [p for p in (r/'out/ort-static-v185-r2').rglob('*') if p.is_file()]
references+=list((r/'out/v214-release-20261030').iterdir())
for folder in ('voice-build-v214-r2','voice-regression-v214-r6','voice-full-v214-r1','voice-package-v214-r1'):
    references+=[p for p in (r/'out'/folder).rglob('*') if p.is_file() and p.suffix in ('.log','.json','.txt','.su')]
spec=importlib.util.spec_from_file_location('checkpoint',r/'tools/checkpoint-a733-project.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
label='a733-v214-voice-resident-recovery-20261030'
refs=list({str(p.resolve(strict=True)):p.resolve(strict=True) for p in references}.values())
print(json.dumps(m.create(r,label,refs),indent=2))
m.verify(r/'out/checkpoints'/label,True)
print('V214_LOCAL_CHECKPOINT_VERIFIED; v213/user summary preserved')
