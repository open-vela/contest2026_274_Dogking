#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Retain all 16 actual ARM units; include continuous capture implementation."""
from pathlib import Path
p=Path(__file__).with_name('check-a733-voice-arm-v181.py');s=p.read_text()
needle="'audio_app.cxx', 'audio_resample_stream.cxx'";assert s.count(needle)==1
s=s.replace(needle,"'audio_app.cxx', 'audio_capture_live.cxx', 'audio_resample_stream.cxx'",1)
exec(compile(s,str(p),'exec'))
