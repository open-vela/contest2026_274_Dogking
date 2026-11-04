#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Actual v193 wrapper with a mock child: fresh/cache/regeneration routing."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
repo=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='camera-v193-wrapper-') as tmp:
    root=Path(tmp)
    (root/'tools').mkdir()
    shutil.copyfile(repo/'tools/build-a733-camera-v193.sh',root/'tools/driver.sh')
    (root/'tools/build-a733-camera-v192.sh').write_text(
        '#!/bin/bash\nset -eu\n'
        'printf "%s\\n" "${A733_REGENERATE_CACHE:-0}" "$@" > "$MOCK_CALLS"\n')
    cache=root/'sdk/cmake_out/cubie-a7z_nsh_v192_camera'
    cache.mkdir(parents=True)
    for mode in ('fresh','cached','regenerate'):
        calls=root/(mode+'.txt')
        if mode!='fresh':(cache/'CMakeCache.txt').touch()
        if mode=='cached':(cache/'build.ninja').touch()
        if mode=='regenerate':(cache/'build.ninja').unlink()
        env={**os.environ,'OPENVELA_WORKSPACE':str(root/'sdk'),'MOCK_CALLS':str(calls)}
        env.pop('A733_REGENERATE_CACHE',None)
        subprocess.run(['bash',str(root/'tools/driver.sh'),'out/report','voice-prefix'],
                       env=env,check=True,timeout=10)
        expected=['1' if mode=='regenerate' else '0','out/report','voice-prefix']
        if mode!='fresh':expected.append('--incremental')
        assert calls.read_text().splitlines()==expected
    print('PASS: actual v193 wrapper fresh/cached/interrupted-cache routes; MOCK child only, no SDK modified')
