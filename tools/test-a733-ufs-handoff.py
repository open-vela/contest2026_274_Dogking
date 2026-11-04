#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Unchanged production handoff observer: read-only mocked callbacks, no hardware."""
import os
import subprocess
import tempfile
from pathlib import Path
repo=Path(__file__).resolve().parent.parent
chip=repo/'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733'
with tempfile.TemporaryDirectory(prefix='ufs-handoff-') as folder:
    exe=Path(folder)/'test'
    subprocess.run(['gcc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
        '-fno-pie','-no-pie','-fsanitize=address,undefined','-I'+str(chip),
        str(chip/'a733_ufs_handoff.c'),str(chip/'a733_ufs_board.c'),
        str(repo/'tools/test-a733-ufs-handoff.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=20,
        env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1'})
