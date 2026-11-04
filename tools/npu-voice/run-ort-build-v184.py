#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run staged minimal ORT configuration/build without nested shell quoting.

Linux/WSL host only. Explicit log prefix is NEW for every attempt. Source,
dependencies and actual NuttX ABI command come from prepare-ort-build-v183.py.
No SDK changes, network, model inference or target access.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import time

REPO = Path(__file__).resolve().parents[2]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('work', type=Path)
    p.add_argument('phase', choices=('configure', 'build'))
    p.add_argument('new_log_prefix', type=Path)
    p.add_argument('--jobs', type=int, default=6)
    p.add_argument('--keep-going', action='store_true', help='Collect independent compile failures; never execute target')
    a = p.parse_args()
    work = a.work.resolve(strict=True)
    prefix = a.new_log_prefix.resolve()
    assert work.is_relative_to(REPO / 'out') and prefix.is_relative_to(REPO / 'out')
    assert 1 <= a.jobs <= 12
    env = dict(os.environ)
    env['PYTHONPATH'] = str(REPO / 'out/ort-hostdeps-v183')
    env['PYTHONDONTWRITEBYTECODE'] = '1'
    started = time.monotonic()
    steps = []
    if a.phase == 'configure':
        cmake_source = work / 'source/cmake/CMakeLists.txt'
        marker = '# The NuttX port accepts the reduced, float-only Matcha ORT graph.'
        if marker not in cmake_source.read_text():
            patch = REPO / 'patches/onnxruntime-v1.23.2-nuttx-cmake.patch'
            subprocess.run(['patch', '--batch', '--forward', '-p1', '-i', str(patch)],
                           cwd=work / 'source', check=True)
        # Upstream's reducer reads training registration files even when no
        # training kernel is enabled. Repair only the missing generated stage,
        # using the exact pinned archive; never modify the reference or SDK.
        if not (work / 'source/orttraining/orttraining/training_ops/cpu/cpu_training_kernels.cc').exists():
            archive = REPO / 'out/onnxruntime-a83fc4d-source.tar.gz'
            assert hashlib.sha256(archive.read_bytes()).hexdigest() == '7270fab0d96d964959b35648014230c00fa2a9cb98df087e5fd5c2b2c24c8085'
            with tarfile.open(archive) as t:
                for member in t:
                    parts = Path(member.name).parts
                    if len(parts) < 2 or parts[1] != 'orttraining' or member.issym() or member.islnk():
                        continue
                    member.name = str(Path(*parts[1:]))
                    target = work / 'source' / member.name
                    if target.is_file():
                        assert target.read_bytes() == t.extractfile(member).read(), 'staged source changed'
                        continue
                    t.extract(member, work / 'source', filter='data')
        config = REPO / 'out/matcha-ort-v183-r1/model-steps-3.required_operators.basic.config'
        commands = [
            [sys.executable, str(work / 'source/tools/ci_build/reduce_op_kernels.py'),
             str(config), '--cmake_build_dir', str(work / 'build')],
            json.loads((work / 'configure-command.json').read_text())]
        commands[-1].append('-Donnxruntime_NUTTX=ON')
    else:
        logging_source = work / 'source/onnxruntime/core/common/logging/logging.cc'
        if '#elif defined(__NuttX__)' not in logging_source.read_text():
            subprocess.run(['patch', '--batch', '--forward', '-p1', '-i',
                            str(REPO / 'patches/onnxruntime-v1.23.2-nuttx-logging.patch')],
                           cwd=work / 'source', check=True)
        for relative, marker, name in [
            ('onnxruntime/core/platform/posix/env.cc','NuttX static CPU profile: dynamic libraries disabled',
             'onnxruntime-v1.23.2-nuttx-env.patch'),
            ('cmake/onnxruntime_providers_cpu.cmake','matcha_ops_sha256',
             'onnxruntime-v1.23.2-nuttx-matcha-profile.patch'),
            ('cmake/onnxruntime_providers_cpu.cmake','core FP16 Conv object references',
             'onnxruntime-v1.23.2-nuttx-link-deps.patch')]:
            if marker not in (work / 'source' / relative).read_text():
                subprocess.run(['patch', '--batch', '--forward', '-p1', '-i',
                                str(REPO / 'patches' / name)],cwd=work / 'source',check=True)
        elf = work / 'build/_deps/abseil_cpp-src/absl/debugging/internal/elf_mem_image.h'
        if '#if defined(__ELF__) && !defined(__NuttX__)' not in elf.read_text():
            subprocess.run(['patch', '--batch', '--forward', '-p1', '-i',
                            str(REPO / 'patches/abseil-20250512-nuttx-elf.patch')],
                           cwd=work / 'build/_deps/abseil_cpp-src', check=True)
        time_format = work / 'build/_deps/abseil_cpp-src/absl/time/internal/cctz/src/time_zone_format.cc'
        if '// NuttX exposes strptime only' not in time_format.read_text():
            subprocess.run(['patch', '--batch', '--forward', '-p1', '-i',
                            str(REPO / 'patches/abseil-20250512-nuttx-time.patch')],
                           cwd=work / 'build/_deps/abseil_cpp-src', check=True)
        args = json.loads((work / 'configure-command.json').read_text())
        # Build the actual static runtime closure, not upstream's unrelated
        # standalone q4 quantization CLI (a bare-metal library is not Linux).
        # Static session archives do not necessarily rebuild all sibling
        # archives after CMake regeneration. Request every ORT static target
        # and its declared dependency closure, not unrelated Abseil utilities
        # (e.g. debug poison/mmap) or bare-metal CLI executables/-ldl.
        ninja=next(x.split('"')[1] for x in (work/'nuttx-toolchain.cmake').read_text().splitlines()
                   if x.startswith('set(CMAKE_MAKE_PROGRAM '))
        listed=subprocess.check_output([ninja,'-C',str(work/'build'),'-t','targets','all'],text=True)
        archives=sorted({line.rsplit(': ',1)[0] for line in listed.splitlines()
                         if ': ' in line and line.rsplit(': ',1)[0].endswith('.a')
                         and Path(line.rsplit(': ',1)[0]).name.startswith('libonnxruntime_')
                         and 'STATIC_LIBRARY_LINKER' in line.rsplit(': ',1)[1]})
        assert 'libonnxruntime_common.a' in archives and 'libonnxruntime_providers.a' in archives
        commands = [[args[0], '--build', str(work / 'build'), '--target', 'onnxruntime_session',*archives,
                     '--parallel', str(a.jobs)]]
        if a.keep_going:
            commands[0] += ['--', '-k', '0']
    for i, command in enumerate(commands):
        path = prefix.with_name(prefix.name + '-%d.log' % i)
        with path.open('xb') as log:
            rc = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                                env=env, cwd=REPO, timeout=1800).returncode
        steps.append(dict(command=command, exit=rc, log=str(path)))
        print(path.name, 'exit', rc, flush=True)
        if rc:
            break
    result = dict(phase=a.phase, steps=steps, elapsed_seconds=time.monotonic() - started,
                  target_hardware_executed=False, full_voice_chain=False)
    with prefix.with_name(prefix.name + '-result.json').open('x') as f:
        json.dump(result, f, indent=2)
    raise SystemExit(0 if all(s['exit'] == 0 for s in steps) else 1)


if __name__ == '__main__':
    main()
