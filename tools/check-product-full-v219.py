#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Keep every product/camera/ABI/ISA gate, allow exact Wi-Fi/cloud additions."""
from pathlib import Path
p=Path(__file__).with_name('check-a733-product-v1.py');s=p.read_text()
extra=tuple('apps/system/aipet/'+n for n in ('agent_setup.c','cloud_voice.h','cloud_voice.cxx',
 'cloud_voice_protocol.hxx','cloud_voice_protocol.cxx','cloud_voice_socket.hxx','cloud_voice_socket.cxx'))
extra+=('apps/system/a733wifi/a733wifi_main.c','apps/system/a733wifi/wifi_secret_input.h',
        'vendor/allwinnertech/chips/a733/a733_wifi_usb.c')
needle='\nextra=(';assert s.count(needle)==1
s=s.replace(needle,needle+''.join(repr(n)+',' for n in extra),1)
s=s.replace('paths=309','paths=316').replace('V217_VOICE_CAMERA_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED',
                                         'V219_VOICE_CAMERA_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED')
exec(compile(s,str(p),'exec'))
