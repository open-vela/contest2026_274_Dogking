#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Separate preview recovery, keeping stable v1.1 and external model hashes."""
from pathlib import Path
p=Path(__file__).with_name('checkpoint-product-v11-v219.py');s=p.read_text()
s=s.replace('v11-v219','v12-v220').replace('V11_V219','V12_V220')
s=s.replace('v219-board-feedback-20261031','v219-board-accepted-20261101')
s=s.replace('references=list({',
  "references.append(r/'out/checkpoints/a733-v11-v219-stable-20261101.bundle')\nreferences=list({",1)
exec(compile(s,str(p),'exec'))
