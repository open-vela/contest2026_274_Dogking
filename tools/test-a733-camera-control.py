#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import os
import subprocess
import tempfile
from a733_camera_test_build import compile_control
repo=Path(__file__).resolve().parents[1]
chip=repo/'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733'
with tempfile.TemporaryDirectory(prefix='a733-camera-control-') as tmp:
    exe=compile_control(repo,'test-a733-camera-control.c',tmp)
    subprocess.run([str(exe)],check=True,timeout=20,
                   env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1'})
