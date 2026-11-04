#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""New regular v229 image only. Preserve inherited GPT/models/power readback."""
from pathlib import Path
p=Path(__file__).with_name('package-product-v20pre-v225.py');s=p.read_text().replace('v225','v229').replace('V225','V229')
exec(compile(s,str(p),'exec'))
