#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Retain product ARM compilation checks and compile every new voice unit."""
from pathlib import Path
p=Path(__file__).with_name('check-a733-product-arm-v1.py')
s=p.read_text()
s=s.replace("'mcu_protocol.cxx','mcu_link.cxx')", "'mcu_protocol.cxx','mcu_link.cxx','cloud_voice.cxx','cloud_voice_protocol.cxx','cloud_voice_socket.cxx')")
s=s.replace(" 'apps/system/aipet/product_cloud_config.c':", " 'apps/system/aipet/agent_setup.c':'apps/system/aipet/agent_startup.c',\n 'apps/system/aipet/product_cloud_config.c':")
assert 'cloud_voice_socket.cxx' in s
exec(compile(s,str(p),'exec'))
# Guard the integration failure caught by the complete v218 link: NuttX's
# sys/random.h needs explicit extern C in this C++ transport translation unit.
names=subprocess.check_output([str(sdk/'prebuilts/gcc/linux-x86_64/aarch64-none-elf/bin/aarch64-none-elf-nm'),
                               '-u',str(out/'cloud_voice_socket.cxx.o')],text=True)
assert any(line.strip()=='U getrandom' for line in names.splitlines())
assert '_Z9getrandom' not in names
print('PASS ARM entropy symbol uses C ABI, not C++-mangled getrandom',flush=True)
