#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Seal repair logs; require an independent Windows/.NET whole-image hash."""
from pathlib import Path
import json,os,subprocess
r=Path(__file__).resolve().parents[1]
image=r.parent/'openvela-a733-cubie-a7z-sd-v212-repair-candidate.img'
assert os.name=='nt','run with the bundled native Windows Python'
command=("$repairStream=[System.IO.File]::OpenRead('"+str(image)+"');"
 "$repairHasher=[System.Security.Cryptography.SHA256]::Create();"
 "try {[System.BitConverter]::ToString($repairHasher.ComputeHash($repairStream)).Replace('-','')}"
 "finally {$repairStream.Dispose();$repairHasher.Dispose()}")
digest=subprocess.check_output(['powershell','-NoProfile','-Command',command],text=True).strip().lower()
assert len(digest)==64 and all(c in '0123456789abcdef' for c in digest)
with (r/'out/voice-windows-hash-v212.json').open('x') as f:
    json.dump(dict(sha256=digest,bytes=image.stat().st_size,implementation='Windows native .NET SHA256 stream'),f,indent=2)
p=Path(__file__).with_name('seal-voice-evidence-v211.py');s=p.read_text()
for a,b in (
 ('voice-regression-v211-r3','voice-regression-v212-r1'),
 ('voice-check-v211-r1','repair-check-v212-r2'),
 ('voice-package-v211-r1','repair-package-v212-r1'),
 ('voice-build-v211-r1','voice-build-v212-r2'),
 ('voice-v211-final','repair-v212-final'),
 ('voice-windows-hash-v211','voice-windows-hash-v212'),
 ('V211_SEALED_VOICE_CANDIDATE_NOT_BOARD_TESTED','V212_SEALED_REPAIR_CANDIDATE_NOT_BOARD_TESTED'),
 ("len(voice['tests'])==14","len(voice['tests'])==15"),
 ('voice_regressions=14','voice_regressions=15')):
    assert a in s,a
    s=s.replace(a,b)
exec(compile(s,str(p),'exec'))
