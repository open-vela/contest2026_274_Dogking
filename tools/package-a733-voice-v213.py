#!/usr/bin/env python3
"""New sibling image only; keep v210 sealed base and complete asset/hash checks."""
from pathlib import Path
p=Path(__file__).with_name('package-a733-voice-v211.py')
s=p.read_text()
assert 'base_release=210' in s
s=s.replace('v211','v213').replace('V211_','V213_')
s=s.replace('release=211','release=213')
exec(compile(s,str(p),'exec'))
