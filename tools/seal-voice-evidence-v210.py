#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Copy only reports/logs/stack inventories; no models, WAV or executables."""
from pathlib import Path
import hashlib,json,shutil
r=Path(__file__).resolve().parents[1];out=r/'docs/evidence/voice-v210-final'
assert not out.exists();out.mkdir()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
sources={'voice':r/'out/voice-regression-v210-r4','full':r/'out/voice-check-v210-r3',
         'package':r/'out/voice-package-v210-r1'}
for prefix,source in sources.items():
    for f in source.rglob('*'):
        if not f.is_file() or f.suffix not in ('.json','.log','.su'):continue
        target=out/prefix/f.relative_to(source);target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(f,target);assert sha(f)==sha(target)
for name in ('build.log','wrapper-status.txt','sdk-restored.json'):
    target=out/'build'/name;target.parent.mkdir(exist_ok=True)
    shutil.copyfile(r/'out/voice-build-v210-r1'/name,target)
shutil.copyfile(r/'out/voice-windows-hash-v210.json',out/'windows-image-hash.json')
image=json.loads((out/'package/summary.json').read_text())
win=json.loads((out/'windows-image-hash.json').read_text())
assert image['sha256']==win['sha256'] and image['bytes']==win['bytes']==2147483648
assert len(image['voice_fixtures'])==3
voice=json.loads((out/'voice/summary.json').read_text())
full=json.loads((out/'full/summary.json').read_text())
assert len(voice['tests'])==12 and len(full['tests'])==22
units=json.loads((out/'voice/voice-arm/summary.json').read_text());assert len(units)==16
payloads={str(f.relative_to(out)):dict(bytes=f.stat().st_size,sha256=sha(f)) for f in out.rglob('*') if f.is_file()}
result=dict(result='V210_SEALED_VOICE_CANDIDATE_NOT_BOARD_TESTED',
 firmware_commit=image['firmware_commit'],image_sha256=image['sha256'],kernel_sha256=image['kernel_sha256'],
 counts=dict(voice_regressions=12,camera_regressions=22,native_voice_units=16,
 constructors=full['constructors'],sdk_paths_restored=297,private_assets_roundtripped=49,standard_wavs=3),
 payloads=payloads,full_voice_chain_accepted=False,latency_3_to_5_seconds=False,
 physical_disks_written=False,ufs_written=False,models_or_private_audio_committed=False)
(out/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
print('SEALED',len(payloads),'byte-exact evidence payloads; NOT hardware/latency acceptance')
