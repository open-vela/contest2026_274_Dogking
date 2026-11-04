#!/usr/bin/env python3
"""Retain v212 link/ABI/ISA gates; add only explicit preparation/diagnostic files."""
from pathlib import Path
p=Path(__file__).with_name('check-a733-repair-v212.py')
s=p.read_text()
s=s.replace("extra=('vendor/", "extra=('apps/system/aipet/voice_general_provider.cxx',"
    "'apps/system/aipet/audio_capture_live.cxx',"
    "'vendor/allwinnertech/chips/a733/a733_tensor_runtime.inc','vendor/",1)
s=s.replace('V212_VOICE_CAMERA_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED',
            'V213_VOICE_CAMERA_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED')
exec(compile(s,str(p),'exec'))
