#!/usr/bin/env python3
"""All v212 voice regressions remain mandatory for preparation optimization."""
from pathlib import Path
p=Path(__file__).with_name('test-voice-v212.py')
s=p.read_text().replace('V212_VOICE_HOST_ARM_PASSED_NOT_BOARD',
                         'V213_VOICE_HOST_ARM_PASSED_NOT_BOARD')
exec(compile(s,str(p),'exec'))
