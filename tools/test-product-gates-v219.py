#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""All 19 inherited voice gates + product, real lease/runtime and Wi-Fi."""
from pathlib import Path
p=Path(__file__).with_name('test-product-v1-gates.py');s=p.read_text()
s=s.replace("[('product-core',", "[('resident-lease-v219',['python3','tools/test-resident-lease-v219.py']),\n"
 " ('runtime-lease-v219',['python3','tools/test-product-turn-lease-v219.py']),\n"
 " ('wifi-v219',['python3','tools/test-wifi-v219.py']),\n"
 " ('product-core',",1)
s=s.replace('tools/check-a733-product-arm-v1.py','tools/check-cloud-arm-v218.py')
s=s.replace('V217_VOICE_HOST_ARM_PASSED_NOT_BOARD','V219_VOICE_HOST_ARM_PASSED_NOT_BOARD')
s=s.replace('desktop-pet-v1.0-candidate','desktop-pet-v1.1-v219-candidate')
exec(compile(s,str(p),'exec'))
