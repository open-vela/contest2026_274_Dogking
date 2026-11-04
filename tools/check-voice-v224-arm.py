#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""All six v223 repair units plus actual ASR lifecycle/provider ARM objects."""
from pathlib import Path
p=Path(__file__).with_name('check-voice-fix1-arm.py');s=p.read_text()
s=s.replace("'voice_play_stream.cxx')", "'voice_play_stream.cxx','voice_general_asr.cxx','voice_general_provider.cxx')")
exec(compile(s,str(p),'exec'))
