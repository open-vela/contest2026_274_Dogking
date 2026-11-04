#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Actual ARM compilation of 18 units, including the enlarged LLM API."""
from pathlib import Path
p=Path(__file__).with_name('check-a733-voice-arm-v181.py');s=p.read_text()
s=s.replace("'audio_app.cxx', 'audio_resample_stream.cxx'","'audio_app.cxx', 'audio_capture_live.cxx', 'audio_resample_stream.cxx'",1)
s=s.replace("('aipetllm_modelcheck.c',)","('aipetllm_modelcheck.c','aipetllm_api.cxx')",1)
exec(compile(s,str(p),'exec'))
