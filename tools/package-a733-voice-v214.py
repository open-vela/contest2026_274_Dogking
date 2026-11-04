#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""New regular image only; preserve sealed v210/v213 and original assets."""
from pathlib import Path
p=Path(__file__).with_name('package-a733-voice-v211.py');s=p.read_text()
assert 'base_release=210' in s
s=s.replace('v211','v214').replace('V211_','V214_').replace('release=211','release=214')
exec(compile(s,str(p),'exec'))
