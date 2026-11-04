#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile speech units with real generated NuttX/ARM64 flags, no SDK changes."""
import argparse
import hashlib
import json
import shlex
import subprocess
from pathlib import Path

repo = Path(__file__).resolve().parents[1]
sdk = (repo / '../../quickly-openvela').resolve()
pet = repo / 'board/a733-cubie-a7z/openvela-overlay/apps/system/aipet'
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('output', type=Path)
a = p.parse_args()
a.output = a.output.resolve(); a.output.mkdir()
build = sdk / 'cmake_out/cubie-a7z_nsh_v178_llm_candidate'
ninja = sdk / 'prebuilts/tools/ninja/bin/ninja'
results = {}
for name in ('voice_asr_decode.c', 'voice_tts_frontend.cxx', 'voice_local.cxx',
             'audio_app.cxx', 'pet_routes.cxx'):
    anchor = 'voice_kws.c' if name.endswith('.c') else 'audio_app.cxx'
    target = 'apps/system/aipet/CMakeFiles/apps_aipet.dir/' + anchor + '.o'
    lines = subprocess.check_output([str(ninja), '-C', str(build), '-t', 'commands', target], text=True).splitlines()
    command = next(shlex.split(s) for s in reversed(lines) if ' -c ' in s and str(sdk / 'apps/system/aipet' / anchor) in s)
    if command[0] == 'ccache': command = command[1:]
    command[1:1] = ['-I' + str(pet), '-I' + str(pet.parents[1] / 'include'),
                     '-I' + str(pet.parent / 'a733display'), '-fstack-usage',
                     '-Wframe-larger-than=4096', '-Werror=frame-larger-than=4096']
    command[command.index('-c') + 1] = str(pet / name)
    target = a.output / (name + '.o')
    command[command.index('-o') + 1] = str(target)
    if '-MF' in command: command[command.index('-MF') + 1] = str(a.output / (name + '.d'))
    with (a.output / (name + '.log')).open('x') as f:
        subprocess.run(command, cwd=build, check=True, stdout=f, stderr=subprocess.STDOUT)
    maximum = 0
    for line in target.with_suffix('.su').read_text().splitlines():
        _, size, kind = line.rsplit('\t', 2)
        if kind != 'static' or not size.isdigit() or int(size) > 4096:
            raise ValueError('Unbounded/excessive frame: ' + line)
        maximum = max(maximum, int(size))
    results[name] = {'source_sha256': hashlib.sha256((pet / name).read_bytes()).hexdigest(),
                     'object_sha256': hashlib.sha256(target.read_bytes()).hexdigest(),
                     'real_nuttx_flags': True, 'max_static_frame': maximum}
(a.output / 'summary.json').write_text(json.dumps(results, indent=2))
print(json.dumps(results, indent=2))
