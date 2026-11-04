#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Retain all v211 full-link/ABI/ISA gates; admit only documented v212 repairs."""
from pathlib import Path
p=Path(__file__).with_name('check-a733-voice-v211.py')
s=p.read_text()
marker='allowed={prefix+tail for tail in ('
assert s.count(marker)==1
extra=('vendor/allwinnertech/chips/a733/a733_csi_capture.h',
 'vendor/allwinnertech/chips/a733/a733_csi_capture.c',
 'vendor/allwinnertech/chips/a733/a733_camera_control.c',
 'vendor/allwinnertech/chips/a733/a733_camera_stream.inc',
 'vendor/allwinnertech/chips/a733/a733_i2s0_audio.c',
 'vendor/allwinnertech/chips/a733/a733_i2s0_playback.inc')
s=s.replace(marker,marker+''.join(repr(n)+',' for n in extra),1)
s=s.replace('V211_VOICE_CAMERA_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED',
 'V212_VOICE_CAMERA_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED')
s=s.replace('tools/audit-a733-isa-v210.py','tools/audit-a733-isa-v212.py')
exec(compile(s,str(p),'exec'))
