#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""NEW v221 regular image, unchanged model and boot power readback checks."""
from pathlib import Path
p=Path(__file__).with_name('package-product-v12-v220.py');s=p.read_text()
s=s.replace('v220','v221').replace('V220','V221')
exec(compile(s,str(p),'exec'))
