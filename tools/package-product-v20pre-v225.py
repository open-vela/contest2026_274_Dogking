#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""New v225 image; inherited model/hash/power/GPT readback checks unchanged."""
from pathlib import Path
p=Path(__file__).with_name('package-product-v13fix2-v224.py');s=p.read_text()
s=s.replace('v224','v225').replace('V224','V225')
exec(compile(s,str(p),'exec'))
