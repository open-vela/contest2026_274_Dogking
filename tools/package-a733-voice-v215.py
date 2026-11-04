#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""New regular v215 image only, retaining base hashes and all model checks."""
from pathlib import Path
p=Path(__file__).with_name('package-a733-voice-v214.py');s=p.read_text()
s=s.replace("replace('v211','v214').replace('V211_','V214_').replace('release=211','release=214')",
 "replace('v211','v215').replace('V211_','V215_').replace('release=211','release=215')")
exec(compile(s,str(p),'exec'))
