#!/usr/bin/env python3
"""Local recovery only; includes the untracked user summary without modifying it."""
from pathlib import Path
import importlib.util
import json
r=Path(__file__).resolve().parents[1]
references=[r.parent/'openvela-a733-cubie-a7z-sd-v213-voice-prepare-candidate.img',
            r.parent/'openvela-a733-cubie-a7z-sd-v213-voice-prepare-candidate.img.sha256',
            r.parent/'openvela-a733-cubie-a7z-sd-v212-repair-candidate.img',
            r/'out/checkpoints/a733-v212-repair-recovery-20261030/manifest.json']
references+=list((r/'out/v213-release-20261030').iterdir())
for folder in ('voice-build-v213-r1','voice-regression-v213-r1','voice-full-v213-r1','voice-package-v213-r1'):
    references += [p for p in (r/'out'/folder).rglob('*')
                   if p.is_file() and p.suffix in ('.log','.json','.txt','.su')]
spec=importlib.util.spec_from_file_location('checkpoint',r/'tools/checkpoint-a733-project.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
label='a733-v213-voice-prepare-recovery-20261030'
refs=list({str(p.resolve(strict=True)):p.resolve(strict=True) for p in references}.values())
print(json.dumps(m.create(r,label,refs),indent=2))
m.verify(r/'out/checkpoints'/label,True)
print('V213_LOCAL_CHECKPOINT_VERIFIED; old v212 checkpoint preserved')
