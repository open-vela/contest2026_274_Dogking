#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Seal v216 only after all 19 software gates and real-model reference test."""
from pathlib import Path
p=Path(__file__).with_name('seal-package-voice-v214.py');s=p.read_text()
s=s.replace('v214','v216').replace('V214_','V216_')
s=s.replace('voice-regression-v216-r6','voice-regression-v216-r2')
s=s.replace('voice-resident-candidate.img','voice-quality-candidate.img')
s=s.replace('voice-resident-recovery','voice-quality-recovery')
s=s.replace("len(v['tests'])==17","len(v['tests'])==19").replace('regressions=17','regressions=19').replace('arm_units=17','arm_units=18')
s=s.replace("old_v213_preserved=True","old_v213_preserved=True,old_v214_preserved=True,old_v215_preserved=True,asr_speedup_accepted=False,tts_voice_quality_accepted=False")
s=s.replace('seal.mkdir()',"q=json.loads((r/'out/llm-quality-v216-r4/summary.json').read_text())\nassert q['result']=='V216_REAL_GGUF_REFERENCE_PASSED_NOT_BOARD' and q['firmware_commit']==a.firmware_commit and len(q['context_samples'])==2\nseal.mkdir()")
exec(compile(s,str(p),'exec'))
