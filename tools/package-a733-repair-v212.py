#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""NEW regular v212 image; reuse v211 strict packager and immutable v210 base.
This does not access physical media, reformat UFS or modify older images.
"""
from pathlib import Path
p=Path(__file__).with_name('package-a733-voice-v211.py')
s=p.read_text()
assert "base_release=210" in s and "BASE_SHA='53816b0e" in s
s=s.replace('v211','v212').replace('V211_','V212_')
exec(compile(s,str(p),'exec'))
