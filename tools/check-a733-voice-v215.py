#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Preserve v214 camera/ABI/ISA/runtime checks; exact additive v215 headers."""
from pathlib import Path
p=Path(__file__).with_name('check-a733-voice-v214.py');s=p.read_text()
marker="marker='allowed={prefix+tail for tail in (';assert s.count(marker)==1"
assert s.count(marker)==1
s=s.replace(marker,"extra+=('apps/system/aipet/voice_load_log.hxx','apps/system/aipet/voice_model_storage.hxx',\n"+
 " 'apps/system/aipet/voice_acoustic.cxx','apps/system/aipet/local_llm.hxx',\n"+
 " 'apps/system/aipet/local_llm.cxx','vendor/allwinnertech/chips/a733/a733_tensor_ufs_path.h')\n"+marker)
s=s.replace("s=s.replace(marker,marker+''.join(repr(n)+',' for n in extra),1)",
 "s=s.replace(marker,marker+''.join(repr(n)+',' for n in extra),1)\n"+
 "s=s.replace(\"paths=298\",\"paths=301\")")
s=s.replace("'V214_VOICE_CAMERA_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED'",
 "'V215_VOICE_CAMERA_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED'")
exec(compile(s,str(p),'exec'))
