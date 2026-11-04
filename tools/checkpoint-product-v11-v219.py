#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Private source/history checkpoint plus hashes of sealed external assets."""
from pathlib import Path
import argparse,importlib.util,json
r=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--llm',type=Path,required=True);a=p.parse_args()
release=json.loads((r/'docs/evidence/product-v11-v219/summary.json').read_text())
assert release['result']=='PRODUCT_V11_V219_SOFTWARE_IMAGE_VERIFIED_NOT_BOARD_ACCEPTED'
label=Path(release['recovery_directory']).name
assert release['recovery_directory']=='out/checkpoints/'+label
references=[a.llm.resolve(strict=True)]
for name in (release['image'],release['base_preserved']['image'],
             'openvela-a733-cubie-a7z-sd-v210-voice-latency-candidate.img'):
    references.extend((r.parent/name,r.parent/(name+'.sha256')))
for folder in (release['sealed_directory'],'out/ort-static-v185-r2',
               'out/general-voice-assets-v185-r2','out/v216-release-20261030'):
    references.extend(f for f in (r/folder).rglob('*') if f.is_file())
for folder in (release['build_report_directory'],release['voice_gate_directory'],
               release['full_gate_directory'],release['package_directory'],
               'out/product-v11-v219-voice-base-r1'):
    references.extend(f for f in (r/folder).rglob('*')
                      if f.is_file() and f.suffix in ('.json','.log','.txt','.su','.cmd','.scr'))
references.append(r/'out/v219-board-feedback-20261031/serial-original.txt')
references=list({str(f.resolve(strict=True)):f.resolve(strict=True) for f in references}.values())
spec=importlib.util.spec_from_file_location('checkpoint',r/'tools/checkpoint-a733-project.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
module.create(r,label,references)
manifest=module.verify(r/release['recovery_directory'],True)
print(json.dumps(dict(result='V11_V219_LOCAL_RECOVERY_VERIFIED',head=manifest['head'],
  source_files=len(manifest['files']),external_references=len(references),
  source_archive_private=True,board_accepted=False,PR_created=False,git_pushed=False),indent=2))
