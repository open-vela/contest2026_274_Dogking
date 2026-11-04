#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Actual bounded data transport and scratch/read cores; no real device."""
from pathlib import Path
import os,subprocess,tempfile
repo=Path(__file__).resolve().parent.parent
chip=repo/'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733'
with tempfile.TemporaryDirectory(prefix='ufs-data-') as tmp:
    exe=Path(tmp)/'test'
    subprocess.run(['gcc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
        '-fno-pie','-no-pie','-fsanitize=address,undefined','-I'+str(chip),
        *[str(chip/n) for n in ('a733_ufs_data.c','a733_ufs_scratch.c',
            'a733_ufs_readcheck.c','a733_ufs_handoff.c','a733_ufs_board.c')],
        str(repo/'tools/test-a733-ufs-data.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=120,
        env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1'})
