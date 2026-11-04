#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Sanitize actual camera CPU code using synthetic fixtures; no MMIO or NPU."""
from pathlib import Path
import os
import subprocess
import tempfile

repo = Path(__file__).resolve().parent.parent
overlay = repo / 'board/a733-cubie-a7z/openvela-overlay/apps'
with tempfile.TemporaryDirectory(prefix='a733-camera-image-') as directory:
    binary = Path(directory) / 'test'
    subprocess.run(['gcc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra',
        '-Werror', '-fno-pie', '-no-pie', '-fsanitize=address,undefined',
        '-I' + str(overlay / 'include'),
        str(overlay / 'system/a733camera/a733_camera_image.c'),
        str(repo / 'tools/test-a733-camera-image.c'), '-o', str(binary)],
        check=True)
    subprocess.run([str(binary)], check=True, timeout=20,
        env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=1'})
