#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Execute the actual migration state machine with delayed CPU execution."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='affinity209-') as t:
    exe=Path(t)/'test'
    subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
      '-fsanitize=address,undefined','-fno-pie','-no-pie',
      '-I'+str(r/'board/a733-cubie-a7z/openvela-overlay/apps/include'),
      str(r/'tools/test-a733-affinity-v209.c'),'-o',str(exe)],check=True,timeout=30)
    subprocess.run([str(exe)],check=True,timeout=10)
