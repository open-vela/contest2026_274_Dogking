#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import os,subprocess,tempfile
from a733_camera_test_build import compile_control
repo=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='a733-link-') as tmp:
    exe=compile_control(repo,'test-a733-camera-link.c',tmp)
    subprocess.run([str(exe),str(repo/'out/imx214-mode-v187-r1/imx214-1080p.a7cm')],
        check=True,timeout=60,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1'})
with tempfile.TemporaryDirectory(prefix='a733-rx-') as tmp:
    chip=repo/'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733'
    exe=Path(tmp)/'test'
    subprocess.run(['gcc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
        '-fno-pie','-no-pie','-fsanitize=address,undefined','-I'+str(chip),
        str(chip/'a733_mipi_rx.c'),str(repo/'tools/test-a733-mipi-rx.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=60)
