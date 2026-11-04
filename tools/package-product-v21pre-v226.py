#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
p=Path(__file__).with_name('package-product-v20pre-v225.py');s=p.read_text()
s=s.replace('v225','v226').replace('V225','V226')
exec(compile(s,str(p),'exec'))
