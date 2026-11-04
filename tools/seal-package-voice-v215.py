#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Reuse complete v214 sealing with NEW v215 paths and 18 regression gates."""
from pathlib import Path
p=Path(__file__).with_name('seal-package-voice-v214.py');s=p.read_text()
s=s.replace('v214','v215').replace('V214_','V215_')
s=s.replace('voice-regression-v215-r6','voice-regression-v215-r3')
s=s.replace('voice-resident-candidate.img','voice-load-ufs-candidate.img')
s=s.replace('voice-resident-recovery','voice-load-ufs-recovery')
s=s.replace("len(v['tests'])==17","len(v['tests'])==18").replace('regressions=17','regressions=18')
s=s.replace("old_v213_preserved=True","old_v213_preserved=True,old_v214_preserved=True,ufs_driver_speed_changed=False,storage_1000_MBps_accepted=False")
exec(compile(s,str(p),'exec'))
