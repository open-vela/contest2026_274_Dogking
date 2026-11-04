#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import subprocess,tempfile
from a733_camera_test_build import compile_control
repo=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='a733-frame-composition-') as tmp:
    exe=compile_control(repo,'test-a733-camera-frame.c',tmp,
      ['-Wl,-Ttext-segment=0x41000000'])
    subprocess.run([str(exe),str(repo/'out/imx214-mode-v187-r1/imx214-1080p.a7cm')],check=True,timeout=40)
