#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile current MCU sources with real SDK ARM64 flags; no SDK edits."""
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
repo = Path(__file__).resolve().parents[1]
overlay = repo / 'board/a733-cubie-a7z/openvela-overlay'
pet = overlay / 'apps/system/aipet'
database = json.loads((a.build / 'compile_commands.json').read_text())
a.output = a.output.resolve()
a.output.mkdir()
results = {}
for name in ['mcu_protocol.cxx', 'mcu_link.cxx', 'mcu_agent_tool.cxx', 'mcu_tool_bridge.c',
             'aipet_main.cxx', 'pet_routes.cxx', 'audio_app.cxx', 'agent_bridge.c', 'a733_uart4.c']:
    entry = next(e for e in database if e['file'].endswith('/' + name))
    flags = shlex.split(entry['command'])
    source = (overlay / 'vendor/allwinnertech/chips/a733' / name) if name == 'a733_uart4.c' else pet / name
    command = [flags[0], '-I' + str(pet)]
    i = 1
    while i < len(flags):
        if flags[i] in ('-c', '-o', '-MF', '-MT', '-MQ'):
            i += 2
        elif flags[i] in ('-MD', '-MMD'):
            i += 1
        else:
            command.append(flags[i])
            i += 1
    target = a.output / (name + '.o')
    command += ['-fstack-usage', '-Wframe-larger-than=4096', '-Werror=frame-larger-than=4096',
                '-c', str(source), '-o', str(target)]
    with (a.output / (name + '.log')).open('xb') as log:
        subprocess.run(command, cwd=entry['directory'], stdout=log, stderr=subprocess.STDOUT, check=True)
    maximum = 0
    for line in target.with_suffix('.su').read_text().splitlines():
        _, size, kind = line.rsplit('\t', 2)
        if kind != 'static' or not size.isdigit() or int(size) > 4096:
            raise ValueError('Unbounded/excessive frame: ' + line)
        maximum = max(maximum, int(size))
    results[name] = {'source': str(source.relative_to(repo)),
        'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
        'object_sha256': hashlib.sha256(target.read_bytes()).hexdigest(), 'maximum_frame': maximum}
    print(name, 'ARM64 frame <=', maximum)
(a.output / 'summary.json').write_text(json.dumps(results, indent=2))
