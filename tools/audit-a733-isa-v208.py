#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Final ELF CRC opcode presence and local-only optimisation flags."""
from pathlib import Path
import argparse,hashlib,json,shlex,subprocess
p=argparse.ArgumentParser(description=__doc__);p.add_argument('build',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
db=json.loads((a.build/'compile_commands.json').read_text());flags={}
for tail in ('a733_camera_image.c','a733_npu_runtime.c'):
    item=next(x for x in db if x['file'].endswith('/'+tail));cmd=shlex.split(item['command'])
    opts=[x for x in cmd if x in ('-O0','-O1','-O2','-O3','-Os','-Oz')]
    assert opts and opts[-1]=='-O2'
    assert '-ffast-math' not in cmd and '-march=armv8-a' in cmd
    assert not any(x.startswith('-march=') and x!='-march=armv8-a' for x in cmd)
    flags[tail]=cmd
cc=Path(flags['a733_npu_runtime.c'][0])
asm=subprocess.check_output([str(cc.with_name('aarch64-none-elf-objdump')),'-d',str(a.build/'nuttx')],text=True)
assert 'crc32x' in asm and 'crc32b' in asm
result=dict(result='FINAL_ELF_OPTIONAL_IEEE_CRC_AND_LOCAL_O2_VERIFIED',hot_compiler_flags=flags,
 crc32x_instructions=asm.count('crc32x'),crc32b_instructions=asm.count('crc32b'),
 elf_sha256=hashlib.sha256((a.build/'nuttx').read_bytes()).hexdigest(),hardware_verified=False)
assert not a.output.exists();a.output.write_text(json.dumps(result,indent=2)+'\n')
print(result['result'])
