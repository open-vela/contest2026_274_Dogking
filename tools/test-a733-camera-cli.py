#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise actual camera FILE CLI with synthetic fixtures; no HW/NPU."""
from pathlib import Path
import os
import subprocess
import tempfile

repo = Path(__file__).resolve().parent.parent
apps = repo / 'board/a733-cubie-a7z/openvela-overlay/apps'
with tempfile.TemporaryDirectory(prefix='a733-camera-cli-') as tmp:
    root = Path(tmp)
    binary = root / 'camera'
    subprocess.run(['gcc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra',
        '-Werror', '-fno-pie', '-no-pie', '-fsanitize=address,undefined',
        '-I' + str(apps / 'include'),
        '-I' + str(repo / 'tools/camera-host-fixture'),
        str(apps / 'system/a733camera/a733camera_main.c'),
        str(apps / 'system/a733camera/a733_camera_image.c'),
        '-o', str(binary)], check=True)
    env = {**os.environ, 'ASAN_OPTIONS': 'detect_leaks=1'}
    def run(*args, good=True):
        result = subprocess.run([str(binary), *map(str, args)], env=env,
            capture_output=True, text=True, timeout=20)
        assert (result.returncode == 0) == good, result.stdout + result.stderr
        return result
    result = run('selftest')
    assert 'camera-selftest-v191: PASS' in result.stdout
    assert 'NOT camera capture' in result.stdout
    source = root / 'red.rgb'
    source.write_bytes(bytes([255, 0, 0]) * 4)
    output = root / 'new'
    run('prepare', 'rgb24', 2, 2, 6, source, output)
    target = output / 'model-rgb.bin'
    assert target.read_bytes() == bytes([255, 0, 0]) * (640 * 640)
    marker = output / 'ready.txt'
    assert 'live-camera=0' in marker.read_text()
    assert not (output / 'ready.partial').exists()
    marker_bytes = marker.read_bytes()
    run('prepare', 'rgb24', 2, 2, 6, source, output, good=False)
    assert target.read_bytes() == bytes([255, 0, 0]) * (640 * 640)
    assert marker.read_bytes() == marker_bytes
    short = root / 'short.rgb'
    short.write_bytes(b'bad')
    bad_output = root / 'bad-output'
    run('prepare', 'rgb24', 2, 2, 6, short, bad_output, good=False)
    assert not bad_output.exists()
    run('prepare', 'rgb24', '-2', 2, 6, source, bad_output, good=False)
    run('prepare', 'nv12', 2, 2, 6, source, bad_output, good=False)
    run('prepare', 'raw10p-rggb', 2, 2, 6, source, bad_output, good=False)
    assert not bad_output.exists()
    run('capture', good=False)  # There is deliberately no fake capture path.
    print('PASS: actual camera CLI synthetic RGB, strict frame size/format, new-directory publication, no overwrite/capture')
