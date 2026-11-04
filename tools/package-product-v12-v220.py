#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""NEW v220 image; keep every model/persona/SWOUT1 readback check."""
from pathlib import Path
p=Path(__file__).with_name('package-a733-product-v1.py');s=p.read_text()
old="s=s.replace('v211','v217').replace('V211_','V217_').replace('release=211','release=217')"
assert s.count(old)==1
s=s.replace(old,"s=s.replace('v211','v220').replace('V211_','V220_').replace('release=211','release=220')")
exec(compile(s,str(p),'exec'))
