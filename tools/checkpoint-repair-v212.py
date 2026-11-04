#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Windows local recovery archive; preserve untracked user summary too.
Inherit every v211 external artifact hash; never delete/check out user files.
"""
from pathlib import Path
import importlib.util,json
r=Path(__file__).resolve().parents[1]
prior=r/'out/checkpoints/a733-v211-voice-recovery-r2-20261030/manifest.json'
references=[Path(x['path']) for x in json.loads(prior.read_text())['references']]
references += [r.parent/'openvela-a733-cubie-a7z-sd-v212-repair-candidate.img',
 r.parent/'openvela-a733-cubie-a7z-sd-v212-repair-candidate.img.sha256']
references += list((r/'out/v212-release-20261030').iterdir())
for folder in ('voice-build-v212-r2','voice-regression-v212-r1','repair-check-v212-r2','repair-package-v212-r1'):
    references += [p for p in (r/'out'/folder).rglob('*') if p.is_file() and p.suffix in ('.log','.json','.txt')]
unique={str(p.resolve(strict=True)):p.resolve(strict=True) for p in references}
spec=importlib.util.spec_from_file_location('checkpoint',r/'tools/checkpoint-a733-project.py')
checkpoint=importlib.util.module_from_spec(spec);spec.loader.exec_module(checkpoint)
label='a733-v212-repair-recovery-20261030'
print(json.dumps(checkpoint.create(r,label,list(unique.values())),indent=2))
checkpoint.verify(r/'out/checkpoints'/label,True)
print('V212_CHECKPOINT_ALL_REFERENCES_VERIFIED',len(unique))
