#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Create+verify a new v211 recovery; inherit EVERY v210 external reference."""
from pathlib import Path
import importlib.util,json,subprocess
r=Path(__file__).resolve().parents[1];label='a733-v211-voice-recovery-r2-20261030'
assert subprocess.check_output(['git','status','--porcelain'],cwd=r)==b'','commit source/evidence first'
old=r/'out/checkpoints/a733-v210-voice-recovery-r6-20261030/manifest.json'
references=[Path(x['path']) for x in json.loads(old.read_text())['references']]
references += [r.parent/'openvela-a733-cubie-a7z-sd-v211-voice-workspace-candidate.img',
               r.parent/'openvela-a733-cubie-a7z-sd-v211-voice-workspace-candidate.img.sha256']
references += list((r/'out/v211-release-20261030').iterdir())
unique={str(f.resolve(strict=True)):f.resolve(strict=True) for f in references}
spec=importlib.util.spec_from_file_location('checkpoint',r/'tools/checkpoint-a733-project.py')
checkpoint=importlib.util.module_from_spec(spec);spec.loader.exec_module(checkpoint)
print(json.dumps(checkpoint.create(r,label,list(unique.values())),indent=2))
checkpoint.verify(r/'out/checkpoints'/label,True)
print('V211_CHECKPOINT_AND_ALL_INHERITED_EXTERNAL_REFERENCES_VERIFIED',len(unique))
