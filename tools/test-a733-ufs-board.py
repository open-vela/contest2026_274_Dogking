#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile unchanged read-only board production module with hostile callbacks."""
import os
import re
import subprocess
import tempfile
from pathlib import Path

repo = Path(__file__).resolve().parent.parent
chip = repo/'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733'
with tempfile.TemporaryDirectory(prefix='a733-ufs-board-') as folder:
    exe = Path(folder)/'test'
    subprocess.run(['gcc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
        '-fno-pie','-no-pie','-fsanitize=address,undefined','-I'+str(chip),
        str(chip/'a733_ufs_board.c'),str(repo/'tools/test-a733-ufs-board.c'),
        '-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=20,
        env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1'})
    source = (chip/'a733_hwdiag.c').read_text()
    # No old diagnostic side door into a potentially power-gated HCI window.
    assert not re.search(r'0x0?4520[0-9a-f]{3}',source,re.I)
    assert 'HCI-not-read' in source
    print('Legacy /dev/a733-hw UFS HCI side door removed; source guard PASS')
