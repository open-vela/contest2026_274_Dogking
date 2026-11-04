#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Production NOP state machine with host MMIO/cache/translation fixtures."""
import os
import subprocess
import tempfile
from pathlib import Path
repo=Path(__file__).resolve().parent.parent
chip=repo/'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733'
with tempfile.TemporaryDirectory(prefix='a733-ufs-nop-') as tmp:
    exe=Path(tmp)/'test'
    subprocess.run(['gcc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
        '-fno-pie','-no-pie','-fsanitize=address,undefined','-I'+str(chip),
        str(chip/'a733_ufs_nop.c'),str(chip/'a733_ufs_handoff.c'),
        str(chip/'a733_ufs_board.c'),str(repo/'tools/test-a733-ufs-nop.c'),
        '-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=60,
        env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1'})
