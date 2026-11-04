#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Local source/bundle/verified artifact references, preserving v214 and user docs."""
from pathlib import Path
p=Path(__file__).with_name('checkpoint-voice-v214.py');s=p.read_text()
s=s.replace('v214','v215').replace('V214_','V215_')
s=s.replace('voice-resident-candidate.img','voice-load-ufs-candidate.img')
s=s.replace('voice-resident-recovery','voice-load-ufs-recovery')
s=s.replace('voice-regression-v215-r6','voice-regression-v215-r3')
marker="references += [p for p in (r/'out/ort-static-v185-r2')"
assert s.count(marker)==1
s=s.replace(marker,"references += [r.parent/'openvela-a733-cubie-a7z-sd-v214-voice-resident-candidate.img',\n"+
 " r/'out/checkpoints/a733-v214-voice-resident-recovery-20261030/manifest.json']\n"+marker)
exec(compile(s,str(p),'exec'))
