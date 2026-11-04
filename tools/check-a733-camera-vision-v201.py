#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Archive v201 host checks in a NEW directory; never connect to hardware."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BASE = 'c5f4466b3cf4a6affb33134df3ac59569e7d6d43'
KERNEL = '19e2e0772816a44ee9ec671c3eac92f7978b2587a8a8c502c7abd60919cf96c0'
IMAGE = 'a729b7442ffc764063b191d8ac3ebb38c4a2812c18f21cc9dacf2cd02fbaaa33'
BASE_MANIFEST = '5219c6db28080250b95716ecf88bb87df84b2caa11f894c8cc138c394e9ce896'


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        while chunk := stream.read(1024 * 1024):
            h.update(chunk)
    return h.hexdigest()


def git(*args):
    return subprocess.check_output(['git', '--no-optional-locks', '-c', 'core.longpaths=true',
                                    '-c', 'core.autocrlf=false', *args], cwd=ROOT).decode('utf-8').strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--verify-image', action='store_true', help='Read/hash v200 regular-file image; no device I/O')
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists() or not (output.is_relative_to(ROOT / 'out') or
                               output.is_relative_to(ROOT / 'docs/evidence')):
        raise ValueError('Use a NEW output directory under out or docs/evidence')
    output.mkdir(parents=True, exist_ok=False)
    changed = git('diff', BASE, '--name-only', '--', 'board')
    untracked = git('ls-files', '--others', '--exclude-standard', '--', 'board')
    # Do not normalize/rewrite the existing ORT provenance.json: WSL sees its
    # pre-existing CRLF as a Git diff while native Git's worktree filter does not.
    # Compare EVERY firmware file byte-for-byte to the SHA-pinned v200 recovery
    # manifest instead; a real byte change/new/deleted file still stops the check.
    manifest = ROOT / 'out/checkpoints/a733-v200-swout1-startup-candidate-20261028/manifest.json'
    if digest(manifest) != BASE_MANIFEST:
        raise ValueError('Preserved v200 recovery manifest mismatch')
    baseline = json.loads(manifest.read_text(encoding='utf-8'))
    board_files = {k: value for k, value in baseline['files'].items() if k.startswith('board/')}
    if untracked or set(git('ls-files', '--', 'board').splitlines()) != set(board_files):
        raise ValueError('Firmware file inventory changed')
    for name, value in board_files.items():
        path = ROOT / name
        if not path.is_file() or path.is_symlink() or path.stat().st_size != value['bytes'] or digest(path) != value['sha256']:
            raise ValueError('Firmware bytes changed: ' + name)
    kernel = ROOT / 'out/v200-release-20261028/nuttx.bin'
    if not kernel.is_file() or kernel.is_symlink() or digest(kernel) != KERNEL:
        raise ValueError('Sealed v200 kernel identity mismatch')
    tests = ['test-a733-camera-vision-v201.py']
    if os.name == 'posix':
        tests += ['test-a733-camera-export.py', 'test-a733-vision-v198.py', 'test-a733-camera-frame.py']
    checks = []
    for script in tests:
        command = [sys.executable, str(ROOT / 'tools' / script)]
        run = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                             encoding='utf-8', errors='replace', timeout=180,
                             env={**os.environ, 'PYTHONUTF8': '1'})
        log = script.removesuffix('.py') + '.log'
        with (output / log).open('x', encoding='utf-8', newline='\n') as stream:
            stream.write(run.stdout + run.stderr)
        checks.append(dict(script=script, exit=run.returncode, log=log,
                           log_sha256=digest(output / log)))
        print(script, 'PASS' if run.returncode == 0 else 'FAIL', flush=True)
        if run.returncode:
            raise ValueError('Host regression failed; retain evidence: ' + str(output / log))
    files = ['tools/a733-camera-vision-v201.py', 'tools/test-a733-camera-vision-v201.py',
             'tools/check-a733-camera-vision-v201.py',
             'board/a733-cubie-a7z/openvela-overlay/apps/system/a733camera/a733camera_main.c',
             'board/a733-cubie-a7z/openvela-overlay/apps/system/a733camera/a733_camera_image.c',
             'board/a733-cubie-a7z/openvela-overlay/apps/system/a733display/a733display_main.c',
             'board/a733-cubie-a7z/openvela-overlay/apps/system/a733npu/a733npu_main.c',
             'board/a733-cubie-a7z/openvela-overlay/apps/include/system/a733_vision.h']
    report = dict(result='HOST_CHECKS_PASSED_NOT_BOARD_TESTED', version=201,
                  created_utc=datetime.now(timezone.utc).isoformat(), source_head=git('rev-parse', 'HEAD'),
                  preserved_baseline=BASE, firmware_byte_identity_verified=True,
                  firmware_files_verified=len(board_files), baseline_manifest_sha256=BASE_MANIFEST,
                  git_reported_firmware_paths=changed.splitlines(),
                  python=sys.version, platform=platform.platform(), tests=checks,
                  source_sha256={name: digest(ROOT / name) for name in files},
                  kernel_sha256=KERNEL, v200_image_expected_sha256=IMAGE,
                  v200_image_rehashed=False, linux_only_tests_run=os.name == 'posix',
                  ssh_board_connected=False, hardware_verified=False, new_firmware_or_image=False,
                  limitations=['v200 auto-power and real RAW pending', 'No continuous capture/FPS',
                               'No ISP/accuracy acceptance', 'No live SSH protocol acceptance yet'])
    if args.verify_image:
        image = ROOT.parent / 'openvela-a733-cubie-a7z-sd-v200-swout1-raw-candidate.img'
        if not image.is_file() or image.is_symlink() or image.stat().st_size != 2147483648 or digest(image) != IMAGE:
            raise ValueError('Preserved v200 image mismatch')
        report['v200_image_rehashed'] = True
    with (output / 'summary.json').open('x', encoding='utf-8', newline='\n') as stream:
        json.dump(report, stream, indent=2)
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
