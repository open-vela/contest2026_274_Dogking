#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile real ARM64 UFS/board objects without staging or editing SDK files."""
import argparse
import hashlib
import json
import shlex
import subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('build', type=Path)
p.add_argument('output', type=Path)
a = p.parse_args()
repo = Path(__file__).resolve().parent.parent
overlay = repo/'board/a733-cubie-a7z/openvela-overlay'
db = json.loads((a.build/'compile_commands.json').read_text())
a.output = a.output.resolve()
a.output.mkdir()  # refuse existing evidence
results = {}
for unit, source, reference in [
    ('ufs', 'vendor/allwinnertech/chips/a733/a733_ufs.c', 'a733_hwdiag.c'),
    ('ufs-link', 'vendor/allwinnertech/chips/a733/a733_ufs_link.c', 'a733_hwdiag.c'),
    ('ufs-board', 'vendor/allwinnertech/chips/a733/a733_ufs_board.c', 'a733_hwdiag.c'),
    ('ufs-handoff', 'vendor/allwinnertech/chips/a733/a733_ufs_handoff.c', 'a733_hwdiag.c'),
    ('ufs-quiesce', 'vendor/allwinnertech/chips/a733/a733_ufs_quiesce.c', 'a733_hwdiag.c'),
    ('ufs-nop', 'vendor/allwinnertech/chips/a733/a733_ufs_nop.c', 'a733_hwdiag.c'),
    ('ufs-nop-map', 'vendor/allwinnertech/chips/a733/a733_ufs_nop_map.c', 'a733_hwdiag.c'),
    ('ufs-readcheck', 'vendor/allwinnertech/chips/a733/a733_ufs_readcheck.c', 'a733_hwdiag.c'),
    ('ufs-scratch', 'vendor/allwinnertech/chips/a733/a733_ufs_scratch.c', 'a733_hwdiag.c'),
    ('ufs-data', 'vendor/allwinnertech/chips/a733/a733_ufs_data.c', 'a733_hwdiag.c'),
    ('hwdiag', 'vendor/allwinnertech/chips/a733/a733_hwdiag.c', 'a733_hwdiag.c'),
    ('board', 'vendor/allwinnertech/boards/a733/cubie-a7z/src/a7z_boardinit.c', 'a7z_boardinit.c')]:
    entry = next(e for e in db if e['file'].endswith('/'+reference))
    args = shlex.split(entry['command'])
    command = [args[0], '-DCONFIG_A733_UFS_READONLY=1', '-DCONFIG_A733_UFS_HANDOFF_PROBE=1', '-DCONFIG_A733_UFS_QUIESCE=1', '-DCONFIG_A733_UFS_DMA_NOP=1', '-DCONFIG_A733_UFS_READCHECK=1', '-DCONFIG_A733_UFS_BLOCK_RO=1',
               '-DCONFIG_A733_UFS_SCRATCH=1', '-DCONFIG_A733_UFS_DATA=1', '-include', str(overlay/'vendor/allwinnertech/chips/a733/include/a733_peripherals.h'),
               '-I'+str(overlay/'vendor/allwinnertech/chips/a733/include')]
    i = 1
    while i < len(args):
        if args[i] in ['-c', '-o', '-MF', '-MT', '-MQ']:
            i += 2
        elif args[i] in ['-MD', '-MMD']:
            i += 1
        else:
            command.append(args[i]); i += 1
    obj = a.output/(unit+'.o')
    command += ['-fstack-usage', '-Werror=implicit-function-declaration',
                '-Wframe-larger-than=4096', '-Werror=frame-larger-than=4096',
                '-c', str(overlay/source), '-o', str(obj)]
    with (a.output/(unit+'.log')).open('xb') as log:
        subprocess.run(command, cwd=entry['directory'], stdout=log,
                       stderr=subprocess.STDOUT, check=True)
    maximum = 0
    for line in obj.with_suffix('.su').read_text().splitlines():
        name, size, kind = line.rsplit('\t', 2)
        if kind != 'static' or not size.isdigit() or int(size) > 4096:
            raise ValueError('Unsafe stack frame: '+line)
        maximum = max(maximum, int(size))
    results[unit] = dict(source=source, source_sha256=hashlib.sha256((overlay/source).read_bytes()).hexdigest(),
                        object_sha256=hashlib.sha256(obj.read_bytes()).hexdigest(), maximum_frame=maximum)
    header = {'ufs':'a733_ufs_wire.h', 'ufs-link':'a733_ufs_link.h',
              'ufs-board':'a733_ufs_board.h', 'ufs-handoff':'a733_ufs_handoff.h', 'ufs-quiesce':'a733_ufs_quiesce.h',
              'ufs-nop':'a733_ufs_nop.h', 'ufs-nop-map':'a733_ufs_nop.h',
              'ufs-readcheck':'a733_ufs_readcheck.h',
              'ufs-scratch':'a733_ufs_scratch.h',
              'ufs-data':'a733_ufs_data.h',
              'hwdiag':'include/a733_peripherals.h',
              'board':'include/a733_peripherals.h'}[unit]
    header_path = overlay/'vendor/allwinnertech/chips/a733'/header
    results[unit]['project_header_sha256'] = {header: hashlib.sha256(header_path.read_bytes()).hexdigest()}
    if unit == 'ufs':
        board_header = overlay/'vendor/allwinnertech/chips/a733/a733_ufs_board.h'
        results[unit]['project_header_sha256']['a733_ufs_board.h'] = hashlib.sha256(board_header.read_bytes()).hexdigest()
        handoff_header = overlay/'vendor/allwinnertech/chips/a733/a733_ufs_handoff.h'
        results[unit]['project_header_sha256']['a733_ufs_handoff.h'] = hashlib.sha256(handoff_header.read_bytes()).hexdigest()
    if unit in ('ufs', 'ufs-quiesce', 'ufs-nop', 'ufs-nop-map','ufs-readcheck','ufs-scratch','ufs-data'):
        for name in ('a733_ufs_quiesce.h', 'a733_ufs_handoff.h', 'a733_ufs_board.h'):
            results[unit]['project_header_sha256'][name] = hashlib.sha256((overlay/'vendor/allwinnertech/chips/a733'/name).read_bytes()).hexdigest()
        for name in ('a733_ufs_nop.h', 'a733_ufs_wire.h'):
            results[unit]['project_header_sha256'][name] = hashlib.sha256((overlay/'vendor/allwinnertech/chips/a733'/name).read_bytes()).hexdigest()
    if unit in ('ufs','ufs-readcheck','ufs-scratch','ufs-data'):
        name='a733_ufs_readcheck.h'
        results[unit]['project_header_sha256'][name]=hashlib.sha256((overlay/'vendor/allwinnertech/chips/a733'/name).read_bytes()).hexdigest()
    if unit in ('ufs','ufs-scratch','ufs-data'):
        for name in ('a733_ufs_scratch.h','a733_ufs_write_wire.h','a733_ufs_data.h'):
            results[unit]['project_header_sha256'][name]=hashlib.sha256((overlay/'vendor/allwinnertech/chips/a733'/name).read_bytes()).hexdigest()
    if unit in ('ufs','ufs-data'):
        name='a733_ufs_volume.h'
        results[unit]['project_header_sha256'][name]=hashlib.sha256((overlay/'vendor/allwinnertech/chips/a733'/name).read_bytes()).hexdigest()
    if unit == 'ufs-handoff':
        board_header = overlay/'vendor/allwinnertech/chips/a733/a733_ufs_board.h'
        results[unit]['project_header_sha256']['a733_ufs_board.h'] = hashlib.sha256(board_header.read_bytes()).hexdigest()
    (a.output/(unit+'.command.json')).write_text(json.dumps(command, indent=2)+'\n')
(a.output/'summary.json').write_text(json.dumps(dict(result='ARM64_OBJECTS_COMPILED',
    units=results, hardware_verified=False, linked_image=False), indent=2)+'\n')
print(json.dumps(results, indent=2))
