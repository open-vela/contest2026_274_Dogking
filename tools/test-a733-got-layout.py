#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise the real linker/Image gate with GOT slots, without SDK edits/HW."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('sdk', type=Path)
a = p.parse_args()
repo = Path(__file__).resolve().parents[1]
prefix = str(a.sdk.resolve() / 'prebuilts/gcc/linux-x86_64/aarch64-none-elf/bin/aarch64-none-elf-')
linker = (repo / 'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/boards/a733/cubie-a7z/scripts/dramboot.ld').read_text()
source = r'''
.section .image_header,"ax"
.global _image_start
_image_start:
  b start
  .long 0
  .quad 0
  .quad _image_end - _image_start
  .quad 0,0,0,0
  .long 0x644d5241,0
.section .text.start,"ax"
start: b start
.section .rodata,"a"
  .quad 42
.section .data,"aw"
  .quad 1
.section .got,"aw"
  .quad g_cpu_idlestackalloc
.section .got.plt,"aw"
  .quad 0,0,0
.section .bss,"aw",%nobits
  .balign 16
  .skip 128
.section .initstack,"aw",%nobits
  .balign 4096
.global g_cpu_idlestackalloc
g_cpu_idlestackalloc:
  .skip 8192
'''
env = dict(os.environ, A733_NM=prefix+'nm', A733_READELF=prefix+'readelf')
for name in list(env):
    if name.startswith('A733_UFS_'):
        env[name] = '0'
with tempfile.TemporaryDirectory(prefix='a733-got-') as temp:
    root = Path(temp)
    (root/'fixture.S').write_text(source)
    subprocess.run([prefix+'gcc','-c',str(root/'fixture.S'),'-o',str(root/'fixture.o')],check=True)
    for fixed in (False, True):
        text = linker if fixed else linker.replace('    *(.got .got.*)\n','')
        assert fixed or text != linker
        (root/'layout.ld').write_text(text)
        subprocess.run([prefix+'ld','-T',str(root/'layout.ld'),str(root/'fixture.o'),'-o',str(root/'fixture.elf')],check=True,capture_output=True)
        subprocess.run([prefix+'objcopy','-O','binary',str(root/'fixture.elf'),str(root/'Image')],check=True)
        result = subprocess.run(['bash',str(repo/'tools/check-a733-elf-layout.sh'),str(root/'fixture.elf'),str(root/'Image')],env=env,text=True,capture_output=True)
        if fixed:
            assert result.returncode == 0, result.stdout + result.stderr
        else:
            assert result.returncode != 0 and 'unplaced GOT orphan section' in result.stderr, result.stdout + result.stderr
print('PASS: real ARM64 GOT fixture rejected when orphaned, passes inside file-backed RW data; no hardware')
