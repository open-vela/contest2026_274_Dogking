#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Test the actual wrapper cleanup functions in isolated fake directories."""
from pathlib import Path
import subprocess
import tempfile

repo = Path(__file__).resolve().parent.parent
script = (repo/'tools/build-a733-wapi.sh').read_text()
functions = script[script.index('restore_official()'):script.index("trap 'a733_exit_status=")]
assert 'out/sdk-staging' in script and '${TMPDIR:-/tmp}/a733-build' not in script
for mode in ('normal', 'copy-failure', 'signal-during-cleanup'):
    with tempfile.TemporaryDirectory(prefix='a733-cleanup-test-') as folder:
        root = Path(folder)
        sdk = root/'sdk'
        backup = root/'backup'
        for name, data in [('sdk/one.c', b'staged'), ('backup/one.c', b'original'),
                           ('sdk/new.c', b'new'), ('sdk/patched.c', b'patched'),
                           ('backup/patched.c', b'unpatched'),
                           ('sdk/apps/system/aipet/new.c', b'new'),
                           ('backup/apps/system/aipet/old.c', b'old'),
                           ('sdk/apps/system/a733home/new.c', b'new'),
                           ('sdk/apps/system/a733storage/new.c', b'new'),
                           ('backup/apps/system/a733storage/old.c', b'storage-old')]:
            p = root/name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(data)
        injected = ''
        if mode == 'copy-failure':
            injected = 'cp() { if [[ "$2" == "$backup/one.c" ]]; then return 1; fi; command cp "$@"; }\n'
        elif mode == 'signal-during-cleanup':
            injected = 'cp() { kill -TERM $$; command cp "$@"; }\n'
        command = f'''set -euo pipefail
official={str(sdk)!r}
backup={str(backup)!r}
staged_files=(one.c new.c)
patched_files=(patched.c)
aipet_relative=apps/system/aipetllm
aipet_saved=false
pet_saved=true
home_saved=true
home_official="$official/apps/system/a733home"
storage_saved=true
storage_official="$official/apps/system/a733storage"
{functions}
{injected}
restore_official
'''
        result = subprocess.run(['bash', '-c', command], capture_output=True, text=True, timeout=10)
        assert (result.returncode == 0) == (mode != 'copy-failure'), (mode, result.stderr)
        assert backup.exists() == (mode == 'copy-failure')
        if mode != 'copy-failure':
            assert (sdk/'one.c').read_bytes() == b'original'
        assert not (sdk/'new.c').exists()
        assert (sdk/'patched.c').read_bytes() == b'unpatched'
        assert (sdk/'apps/system/aipet/old.c').read_bytes() == b'old'
        assert not (sdk/'apps/system/aipet/new.c').exists()
        assert not (sdk/'apps/system/a733home').exists()
        assert (sdk/'apps/system/a733storage/old.c').read_bytes()==b'storage-old'
        assert not (sdk/'apps/system/a733storage/new.c').exists()
        print('Production SDK cleanup PASS:', mode)
print('Fake directories only; no real SDK, device, or network access')
