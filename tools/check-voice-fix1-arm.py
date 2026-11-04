#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile final v221fix1 voice units, actual target flags, no SDK edits."""
from pathlib import Path
p=Path(__file__).with_name('check-a733-voice-arm-v181.py');s=p.read_text()
start=s.index('units = ');end=s.index('summary = {}',start)
s=s[:start]+"units = [(pet + name, pet + 'audio_app.cxx') for name in ('audio_app.cxx','audio_capture_live.cxx','voice_local.cxx','voice_tts_frontend.cxx','voice_tts_engine.cxx','voice_play_stream.cxx')]\n"+s[end:]
exec(compile(s,str(p),'exec'))
