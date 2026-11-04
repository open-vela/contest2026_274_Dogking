#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Retain v220 full hardware/ABI/ISA gates; declare the MCU Agent change."""
from pathlib import Path
p=Path(__file__).with_name('check-product-full-v220.py');s=p.read_text()
old="extra=('apps/system/aipetllm/aipetllm_prefill.inc',)+tuple("
assert s.count(old)==1
s=s.replace(old,"extra=('apps/system/aipetllm/aipetllm_prefill.inc','apps/system/aipet/mcu_agent_tool.cxx',)+tuple(")
s=s.replace('desktop pet v1.2 PREVIEW (v220;','desktop pet v1.2fix PREVIEW (v221;')
s=s.replace('voice-service-v1.2-preview(v220)','voice-service-v1.2fix-preview(v221)')
s=s.replace('V220_VOICE_CAMERA_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED','V221_VOICE_CAMERA_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED')
exec(compile(s,str(p),'exec'))
