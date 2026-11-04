#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Unmodified production readcheck, adversarial host MMIO/media. No hardware."""
from pathlib import Path
import os,subprocess,tempfile
repo=Path(__file__).resolve().parent.parent
chip=repo/'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733'
with tempfile.TemporaryDirectory(prefix='a733-readcheck-') as tmp:
    exe=Path(tmp)/'test'
    subprocess.run(['gcc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
        '-fno-pie','-no-pie','-fsanitize=address,undefined','-I'+str(chip),
        str(chip/'a733_ufs_readcheck.c'),str(chip/'a733_ufs_handoff.c'),
        str(chip/'a733_ufs_board.c'),str(repo/'tools/test-a733-ufs-readcheck.c'),
        '-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=60,
        env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1'})
