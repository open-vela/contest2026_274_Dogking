#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Retain full camera/voice/ISA gates and exact additive v214 source allowlist."""
from pathlib import Path
import re,subprocess
r=Path(__file__).resolve().parents[1]
header='board/a733-cubie-a7z/openvela-overlay/nuttx/include/sys/a733_audio.h'
old=subprocess.check_output(['git','show','e0b349905acf608532c740e3386dfbe5ad0d51d7:'+header],cwd=r).decode()
new=(r/header).read_text()
for definition in re.findall(r'struct\s+\w+\s*\{.*?\};',old,re.S):assert definition in new
for definition in re.findall(r'^#define A733_AUDIO_.*$',old,re.M):assert definition in new
p=Path(__file__).with_name('check-a733-voice-v211.py');s=p.read_text()
extra=('vendor/allwinnertech/chips/a733/a733_csi_capture.h',
 'vendor/allwinnertech/chips/a733/a733_csi_capture.c',
 'vendor/allwinnertech/chips/a733/a733_camera_control.c',
 'vendor/allwinnertech/chips/a733/a733_camera_stream.inc',
 'vendor/allwinnertech/chips/a733/a733_i2s0_audio.c',
 'vendor/allwinnertech/chips/a733/a733_i2s0_playback.inc',
 'vendor/allwinnertech/chips/a733/a733_tensor_runtime.inc',
 'vendor/allwinnertech/chips/a733/a733_audio_client.c',
 'nuttx/include/sys/a733_audio.h','apps/include/system/a733_pcm.h',
 'apps/system/aipet/aipet_main.cxx','apps/system/aipet/audio_app.hxx',
 'apps/system/aipet/audio_capture_live.cxx','apps/system/aipet/audio_capture_live.hxx',
 'apps/system/aipet/voice_general_provider.cxx','apps/system/aipet/voice_general_provider.hxx',
 'apps/system/aipet/voice_general_asr.hxx','apps/system/aipet/voice_tts_frontend.cxx')
marker='allowed={prefix+tail for tail in (';assert s.count(marker)==1
s=s.replace(marker,marker+''.join(repr(n)+',' for n in extra),1)
s=s.replace('V211_VOICE_CAMERA_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED',
 'V214_VOICE_CAMERA_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED')
s=s.replace('tools/audit-a733-isa-v210.py','tools/audit-a733-isa-v212.py')
exec(compile(s,str(p),'exec'))
