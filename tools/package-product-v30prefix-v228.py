#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""NEW regular v228 image. Preserve every inherited model/GPT/power readback."""
from pathlib import Path
p=Path(__file__).with_name('package-product-v20pre-v225.py');s=p.read_text()
s=s.replace('v225','v228').replace('V225','V228')
exec(compile(s,str(p),'exec'))
