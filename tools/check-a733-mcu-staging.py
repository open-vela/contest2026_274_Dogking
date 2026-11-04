#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Record SDK restoration and verify the actual staged MCU product sources."""
import argparse
import hashlib
import json
import re
from pathlib import Path

repo = Path(__file__).resolve().parents[1]
overlay = repo / 'board/a733-cubie-a7z/openvela-overlay'


def inventory(sdk):
    text = (repo / 'tools/build-a733-wapi.sh').read_text()
    names = set()
    for label in ('files', 'patched_files'):
        body = re.search(r'^' + label + r'=\(\n(.*?)^\)', text, re.M | re.S).group(1)
        names.update(line.strip() for line in body.splitlines() if line.strip())
    for name, source in [('aipet', overlay / 'apps/system/aipet'),
                         ('aipetllm', overlay / 'apps/system/aipetllm'),
                         ('a733home', repo / 'app/a733home'),
                         ('a733storage', repo / 'app/a733storage')]:
        relative = 'apps/system/' + name
        for root in (sdk / relative, source):
            if root.is_dir():
                for p in root.rglob('*'):
                    if p.is_file():
                        names.add(relative + '/' + str(p.relative_to(root)))
        names.add(relative)
    records = {}
    for name in sorted(names):
        p = sdk / name
        if p.is_symlink():
            records[name] = {'link': str(p.readlink())}
        elif p.is_file():
            data = p.read_bytes()
            records[name] = {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
        else:
            records[name] = 'directory' if p.is_dir() else None
    return records


def staged(sdk):
    names = ['apps/system/aipet/' + n for n in
             ('mcu_protocol.hxx', 'mcu_protocol.cxx', 'mcu_link.hxx', 'mcu_link.cxx',
              'mcu_agent_tool.cxx', 'mcu_tool_bridge.c', 'mcu_tool_bridge.h', 'aipet_main.cxx', 'pet_routes.cxx', 'audio_app.cxx',
              'agent_bridge.c', 'CMakeLists.txt')]
    names += ['vendor/allwinnertech/chips/a733/a733_uart4.c']
    records = {}
    for name in names:
        source = (overlay / name).read_bytes()
        if source != (sdk / name).read_bytes():
            raise ValueError('Staging mismatch: ' + name)
        records[name] = hashlib.sha256(source).hexdigest()
    return {'result': 'MCU_STAGED_SOURCE_MATCH', 'files': records}


p = argparse.ArgumentParser(description=__doc__)
p.add_argument('mode', choices=('snapshot', 'restored', 'staged'))
p.add_argument('sdk', type=Path)
p.add_argument('record', type=Path, nargs='?')
a = p.parse_args()
if a.mode == 'staged':
    result = staged(a.sdk)
elif a.mode == 'snapshot':
    result = inventory(a.sdk)
    with a.record.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2)
    print('SDK snapshot:', len(result), 'paths')
    raise SystemExit(0)
else:
    before = json.loads(a.record.read_text())
    after = inventory(a.sdk)
    different = sorted(k for k in before.keys() | after.keys() if before.get(k) != after.get(k))
    if different:
        raise ValueError('SDK NOT RESTORED: ' + repr(different))
    result = {'result': 'SDK_RESTORED_BYTE_EXACT', 'paths': len(after)}
print(json.dumps(result, indent=2))
