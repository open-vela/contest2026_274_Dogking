#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Preserve every v213 gate and run continuous voice lifecycle tests first."""
from pathlib import Path
p=Path(__file__).with_name('test-voice-v211.py')
s=p.read_text().replace("tests=[\n","tests=[\n ('resident',['python3','tools/test-resident-v214.py']),\n ('monitor-driver',['python3','tools/test-monitor-driver-v214.py']),\n ('repair',['python3','tools/test-repair-v212.py']),\n",1)
s=s.replace('V211_VOICE_HOST_ARM_PASSED_NOT_BOARD','V214_VOICE_HOST_ARM_PASSED_NOT_BOARD')
s=s.replace('tools/check-a733-voice-arm-v181.py','tools/check-a733-voice-arm-v214.py')
exec(compile(s,str(p),'exec'))
