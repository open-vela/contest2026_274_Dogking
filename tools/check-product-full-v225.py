#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Preserve full hardware/ABI/ISA gates, exact named application delta only."""
from pathlib import Path
import json
p=Path(__file__).with_name('check-product-full-v219.py');s=p.read_text()
extra=('apps/system/aipetllm/aipetllm_prefill.inc','apps/system/aipet/mcu_agent_tool.cxx',
 'apps/system/aipet/voice_local.cxx','apps/system/aipet/voice_general_asr.cxx',
 'apps/system/aipet/voice_general_provider.cxx','apps/system/aipet/mcu_protocol.hxx',
 'apps/system/aipet/mcu_link.hxx',
 'apps/system/a733services/services_config.c','apps/system/a733services/a733services_main.c')
needle="needle='\\nextra=(';";assert s.count(needle)==1
s=s.replace(needle,'extra+='+repr(extra)+'\n'+needle)
s=s.replace('V219_VOICE_CAMERA_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED',
            'V225_VOICE_CAMERA_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED')
exec(compile(s,str(p),'exec'))
assert 'CONFIG_A733_PRODUCT_V12=y' in (a.build/'.config').read_text()
binary=(a.build/'nuttx.bin').read_bytes()
for label in (b'desktop pet v2.0pre CANDIDATE (v225;',b'voice-service-v2.0pre(v225)',
 b'local_llm.context.v12',b'gate-end-v224:',b'resident-ASR-discard-no-final-NPU',
 b'mcu-desktop-v225:',b'product-setting-v225:',b'product-boot-v225:',b'--pet-boot'):
    assert label in binary,label
f=a.output/'summary.json';report=json.loads(f.read_text())
report.update(default_local_history_pairs=0,desktop_session_commands=True,
 spoken_numeric_steps_scale=10,voice_move_speed=2,boot_listening_default=False,board_tested=False)
f.write_text(json.dumps(report,indent=2)+'\n')
