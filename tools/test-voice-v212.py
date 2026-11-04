#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Extend the preserved v211 regression runner, without copying its tests."""
from pathlib import Path
p=Path(__file__).with_name('test-voice-v211.py')
s=p.read_text()
assert "tests=[\n" in s and 'V211_VOICE_HOST_ARM_PASSED_NOT_BOARD' in s
s=s.replace("tests=[\n","tests=[\n ('repair',['python3','tools/test-repair-v212.py']),\n",1)
s=s.replace('V211_VOICE_HOST_ARM_PASSED_NOT_BOARD','V212_VOICE_HOST_ARM_PASSED_NOT_BOARD')
exec(compile(s,str(p),'exec'))
