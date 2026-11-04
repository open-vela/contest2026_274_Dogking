#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Archive local v1 sources/history and hash immutable release references."""
from pathlib import Path
import argparse
import importlib.util
import json

r = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--llm', type=Path, required=True)
p.add_argument('--evidence-name', choices=('product-v1','product-v1-audit'), default='product-v1')
a = p.parse_args()
release = json.loads((r/'docs/evidence'/a.evidence_name/'summary.json').read_text())
assert release['result'] == 'PRODUCT_V1_SOFTWARE_IMAGE_VERIFIED_NOT_BOARD_ACCEPTED'
assert release['bytes'] == 2147483648
refs = [
    r.parent/release['image'],
    r.parent/(release['image']+'.sha256'),
    a.llm.resolve(strict=True),
    r.parent/'openvela-a733-cubie-a7z-sd-v216-voice-quality-candidate.img',
    r.parent/'openvela-a733-cubie-a7z-sd-v216-voice-quality-candidate.img.sha256',
    r.parent/'openvela-a733-cubie-a7z-sd-v204-camera-yolo-live-candidate.img',
    r.parent/'openvela-a733-cubie-a7z-sd-v204-camera-yolo-live-candidate.img.sha256',
    r/'out/checkpoints/a733-v216-voice-quality-recovery-20261030/manifest.json',
    r/'out/general-voice-assets-v185-r2/manifest.json',
]
for folder in (release['sealed_directory'], 'out/ort-static-v185-r2'):
    refs += [f for f in (r/folder).rglob('*') if f.is_file()]
for folder in ('product-v1-voice-regression-r1',
               'llm-quality-v216-r4'):
    refs += [f for f in (r/'out'/folder).rglob('*')
             if f.is_file() and f.suffix in ('.json', '.log', '.txt', '.su')]
refs += [f for f in (r/release['package_directory']).rglob('*') if f.is_file()]
for folder in (release.get('voice_gate_directory','out/product-v1-gates-r3'),
               release.get('full_gate_directory','out/product-v1-full-r1'),
               release.get('build_report_directory','out/product-v1-build-r4')):
    refs += [f for f in (r/folder).rglob('*') if f.is_file() and f.suffix in ('.json','.log','.txt','.su')]
refs = list({str(f.resolve(strict=True)): f.resolve(strict=True) for f in refs}.values())
voice_summary=r/release.get('voice_gate_directory','out/product-v1-gates-r3')/'summary.json'
baseline=json.loads(voice_summary.read_text()).get('baseline_directory')
if baseline:
    folder=(r/baseline).resolve(strict=True);assert folder.is_relative_to(r/'out')
    refs += [f for f in folder.rglob('*') if f.is_file() and f.suffix in ('.json','.log','.txt','.su')]
if a.evidence_name=='product-v1-audit':
    original=json.loads((r/'docs/evidence/product-v1/summary.json').read_text())
    refs += [r.parent/original['image'],r.parent/(original['image']+'.sha256'),
             r/original['recovery_directory']/'manifest.json']
refs = list({str(f.resolve(strict=True)): f.resolve(strict=True) for f in refs}.values())
spec = importlib.util.spec_from_file_location('checkpoint', r/'tools/checkpoint-a733-project.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
label = Path(release['recovery_directory']).name
assert release['recovery_directory']=='out/checkpoints/'+label
module.create(r, label, refs)
manifest = module.verify(r/'out/checkpoints'/label, True)
print(json.dumps(dict(result='V1_LOCAL_RECOVERY_VERIFIED', head=manifest['head'],
                     source_files=len(manifest['files']), references=len(refs),
                     hardware_accepted=False), indent=2))
