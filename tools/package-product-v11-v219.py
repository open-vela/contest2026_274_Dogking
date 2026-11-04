#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Preserve the private-asset roundtrips and SWOUT1 guard; NEW v219 file only."""
from pathlib import Path
p=Path(__file__).with_name('package-a733-product-v1.py');s=p.read_text()
old="s=s.replace('v211','v217').replace('V211_','V217_').replace('release=211','release=217')"
assert s.count(old)==1
s=s.replace(old,"s=s.replace('v211','v219').replace('V211_','V219_').replace('release=211','release=219')")
exec(compile(s,str(p),'exec'))
