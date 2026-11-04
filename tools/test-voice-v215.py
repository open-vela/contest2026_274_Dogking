#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""All v214 gates, plus load/UFS selector/CRC tests; no physical hardware."""
from pathlib import Path
p=Path(__file__).with_name('test-voice-v214.py');s=p.read_text()
s=s.replace("s=s.replace('V211_VOICE_HOST_ARM_PASSED_NOT_BOARD','V214_VOICE_HOST_ARM_PASSED_NOT_BOARD')",
 "s=s.replace(\"tests=[\\n\",\"tests=[\\n ('load-storage',['python3','tools/test-voice-load-storage-v215.py']),\\n\",1)\n"+
 "s=s.replace('V211_VOICE_HOST_ARM_PASSED_NOT_BOARD','V215_VOICE_HOST_ARM_PASSED_NOT_BOARD')")
exec(compile(s,str(p),'exec'))
