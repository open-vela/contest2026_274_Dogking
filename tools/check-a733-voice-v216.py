#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Reuse all prior protected hardware/ABI/ISA/camera gates, exact app additions."""
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
 'vendor/allwinnertech/chips/a733/a733_csi_capture.c','vendor/allwinnertech/chips/a733/a733_camera_control.c',
 'vendor/allwinnertech/chips/a733/a733_camera_stream.inc','vendor/allwinnertech/chips/a733/a733_i2s0_audio.c',
 'vendor/allwinnertech/chips/a733/a733_i2s0_playback.inc','vendor/allwinnertech/chips/a733/a733_tensor_runtime.inc',
 'vendor/allwinnertech/chips/a733/a733_audio_client.c','nuttx/include/sys/a733_audio.h',
 'apps/include/system/a733_pcm.h','apps/system/aipet/aipet_main.cxx','apps/system/aipet/audio_app.hxx',
 'apps/system/aipet/audio_capture_live.cxx','apps/system/aipet/audio_capture_live.hxx',
 'apps/system/aipet/voice_general_provider.cxx','apps/system/aipet/voice_general_provider.hxx',
 'apps/system/aipet/voice_general_asr.hxx','apps/system/aipet/voice_tts_frontend.cxx',
 'apps/system/aipet/voice_load_log.hxx','apps/system/aipet/voice_model_storage.hxx',
 'apps/system/aipet/voice_acoustic.cxx','apps/system/aipet/local_llm.hxx','apps/system/aipet/local_llm.cxx',
 'vendor/allwinnertech/chips/a733/a733_tensor_ufs_path.h',
 'apps/system/aipet/pet_persona.hxx','apps/system/aipet/pet_routes.cxx',
 'apps/system/aipet/audio_resample.hxx','apps/system/aipet/audio_resample_stream.cxx',
 'apps/system/aipet/voice_local.hxx','apps/system/aipet/voice_phrase_pipe.hxx',
 'apps/system/aipet/voice_play_stream.hxx','apps/system/aipet/voice_tts_frontend.hxx',
 'apps/system/aipetllm/aipetllm_api.cxx','apps/system/aipetllm/aipetllm_api.h',
 'apps/system/aipetllm/aipetllm_sequence.h')
marker='allowed={prefix+tail for tail in (';assert s.count(marker)==1
s=s.replace(marker,marker+''.join(repr(n)+',' for n in extra),1)
s=s.replace('paths=298','paths=302').replace('V211_VOICE_CAMERA_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED','V216_VOICE_CAMERA_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED')
s=s.replace('tools/audit-a733-isa-v210.py','tools/audit-a733-isa-v212.py')
exec(compile(s,str(p),'exec'))
