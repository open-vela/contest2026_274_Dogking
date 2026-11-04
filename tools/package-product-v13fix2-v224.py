#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
p=Path(__file__).with_name('package-product-v13fix-v223.py');s=p.read_text()
s=s.replace('v223','v224').replace('V223','V224')
exec(compile(s,str(p),'exec'))
