#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Keep v223 full checks; declare the two changed ASR lifecycle units."""
from pathlib import Path
p=Path(__file__).with_name('check-product-full-v223.py');s=p.read_text()
s=s.replace("'apps/system/aipet/voice_local.cxx',)+tuple(",
 "'apps/system/aipet/voice_local.cxx','apps/system/aipet/voice_general_asr.cxx','apps/system/aipet/voice_general_provider.cxx',)+tuple(")
s=s.replace('v223','v224').replace('V223','V224').replace('v1.3fix','v1.3fix2')
exec(compile(s,str(p),'exec'))
