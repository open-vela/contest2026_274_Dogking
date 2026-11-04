#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Sanitize pure CSI candidate buffer arithmetic, no hardware or mapping."""
from pathlib import Path
import os
import subprocess
import tempfile
repo=Path(__file__).resolve().parent.parent
chip=repo/'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733'
with tempfile.TemporaryDirectory(prefix='a733-csi-plan-') as tmp:
    binary=Path(tmp)/'test'
    subprocess.run(['gcc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
        '-fno-pie','-no-pie','-fsanitize=address,undefined','-I'+str(chip),
        str(chip/'a733_csi_frame_plan.c'),str(repo/'tools/test-a733-csi-frame-plan.c'),
        '-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=10,
        env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1'})
