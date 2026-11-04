#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Actual cold-control core under sanitizers with synthetic PMIC/MMIO only."""
from pathlib import Path
import os
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
chip = repo/'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733'
with tempfile.TemporaryDirectory(prefix='a733-imx214-cold-') as tmp:
    binary = Path(tmp)/'test'
    subprocess.run(['gcc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
        '-fno-pie','-no-pie','-fsanitize=address,undefined', '-I'+str(chip),
        str(chip/'a733_imx214_cold.c'),str(chip/'a733_imx214_twi3.c'),
        str(chip/'a733_imx214_probe.c'),str(repo/'tools/test-a733-imx214-cold.c'),
        str(chip/'a733_imx214_mode.c'),
        '-o',str(binary)],check=True,timeout=30)
    subprocess.run([str(binary)],check=True,timeout=20,
                   env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1'})
