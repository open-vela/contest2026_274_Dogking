#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""WSL/Linux: stage pinned ORT sources/deps and actual NuttX ABI toolchain.

Creates a NEW work directory. Does not edit the original third-party source,
official SDK, compiler or models. No network, target execution or success claim.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import tarfile
import zipfile

REPO = Path(__file__).resolve().parents[2]
SDK = (REPO / '../../quickly-openvela').resolve()
BUILD = SDK / 'cmake_out/cubie-a7z_nsh_v182_live_capture'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('output', type=Path)
    p.add_argument('--sdk-build', type=Path, default=BUILD)
    a = p.parse_args()
    build = a.sdk_build.resolve(strict=True)
    assert build.is_relative_to(SDK / 'cmake_out')
    a.output = a.output.resolve()
    a.output.mkdir()
    work = a.output
    source = work / 'source'
    source.mkdir()
    archive = REPO / 'out/onnxruntime-a83fc4d-source.tar.gz'
    assert hashlib.sha256(archive.read_bytes()).hexdigest() == '7270fab0d96d964959b35648014230c00fa2a9cb98df087e5fd5c2b2c24c8085'
    with tarfile.open(archive) as t:
        # Test models account for most of the source archive. They are not
        # needed for this static no-tests build and are never executed here.
        for member in t:
            parts = Path(member.name).parts
            relative = Path(*parts[1:])
            if not parts[1:] or '..' in relative.parts or member.issym() or member.islnk():
                continue
            if relative.parts[0] not in ('cmake', 'include', 'onnxruntime', 'orttraining', 'tools') and len(relative.parts) > 1:
                continue
            if str(relative).startswith(('onnxruntime/test/testdata', 'onnxruntime/test/onnx')):
                continue
            member.name = str(relative)
            t.extract(member, source, filter='data')
    deps = REPO / 'out/ort-deps-v183-r2'
    manifest = json.loads((deps / 'manifest.json').read_text())
    for item in manifest['files']:
        original = deps / item['archive']
        assert hashlib.sha256(original.read_bytes()).hexdigest() == item['sha256']
        target = source / 'mirror' / item['relative']
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(original, target)
    protoc = work / 'host-protoc'
    protoc.mkdir()
    with zipfile.ZipFile(deps / 'protoc_linux_x64.zip') as z:
        z.extractall(protoc)
    (protoc / 'bin/protoc').chmod(0o755)
    ninja = SDK / 'prebuilts/tools/ninja/bin/ninja'
    target = 'apps/system/aipet/CMakeFiles/apps_aipet.dir/audio_app.cxx.o'
    lines = subprocess.check_output([str(ninja), '-C', str(build), '-t', 'commands', target], text=True).splitlines()
    command = next(shlex.split(line) for line in reversed(lines) if ' -c ' in line and 'audio_app.cxx' in line)
    if command[0] == 'ccache':
        command = command[1:]
    cpp = Path(command[0])
    cc = cpp.with_name(cpp.name.replace('g++', 'gcc'))
    systems = []
    for i, value in enumerate(command):
        if value == '-isystem':
            systems.extend([value, command[i + 1]])
    cpp_includes = [arg for arg in command if arg.startswith('-I') and
                    any(x in arg for x in ('include/libcxx', 'include/libcxxabi', '/nuttx/libs/libxx/libcxx/'))]
    common = ['-D__NuttX__', '-D_LDBL_EQ_DBL', '-march=armv8-a', '-fno-strict-aliasing',
              '-fno-common', '-ffunction-sections', '-fdata-sections', *systems]
    cxxflags = common + ['-nostdinc++', '-fno-exceptions', '-fno-rtti', '-fcheck-new', *cpp_includes]
    def quoted(values):
        return shlex.join(map(str, values)).replace('"', '\\"')
    toolchain = f'''# Generated from actual v182 NuttX command. Not a Linux sysroot.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_C_COMPILER "{cc}")
set(CMAKE_CXX_COMPILER "{cpp}")
set(CMAKE_ASM_COMPILER "{cc}")
set(CMAKE_C_FLAGS_INIT "{quoted(common)}")
set(CMAKE_CXX_FLAGS_INIT "{quoted(cxxflags)}")
set(CMAKE_ASM_FLAGS_INIT "-march=armv8-a")
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG" CACHE STRING "" FORCE)
set(CMAKE_MAKE_PROGRAM "{ninja}" CACHE FILEPATH "" FORCE)
set(CMAKE_HAVE_LIBC_PTHREAD TRUE CACHE BOOL "" FORCE)
set(HAS_REALLOCARRAY FALSE CACHE BOOL "" FORCE)
'''
    (work / 'nuttx-toolchain.cmake').write_text(toolchain)
    cmake = REPO / 'out/ort-tools-v183/unpacked/cmake/data/bin/cmake'
    config = REPO / 'out/matcha-ort-v183-r1/model-steps-3.required_operators.basic.config'
    definitions = dict(CMAKE_TOOLCHAIN_FILE=work / 'nuttx-toolchain.cmake', CMAKE_BUILD_TYPE='Release',
                       onnxruntime_MINIMAL_BUILD='ON', onnxruntime_DISABLE_EXCEPTIONS='ON',
                       onnxruntime_DISABLE_RTTI='ON', onnxruntime_DISABLE_ML_OPS='ON',
                       onnxruntime_DISABLE_CONTRIB_OPS='ON', onnxruntime_BUILD_SHARED_LIB='OFF',
                       onnxruntime_BUILD_UNIT_TESTS='OFF', onnxruntime_ENABLE_CPUINFO='OFF',
                       onnxruntime_ENABLE_TRAINING='OFF', onnxruntime_ENABLE_TRAINING_APIS='OFF',
                       onnxruntime_USE_XNNPACK='OFF', onnxruntime_ENABLE_LTO='OFF',
                       onnxruntime_USE_MIMALLOC='OFF', onnxruntime_REDUCED_OPS_BUILD='ON',
                       onnxruntime_REDUCED_OPS_BUILD_CONFIG=config, onnxruntime_BUILD_BENCHMARKS='OFF',
                       onnxruntime_target_platform='aarch64', FLATBUFFERS_BUILD_FLATC='OFF',
                       ONNX_CUSTOM_PROTOC_EXECUTABLE=protoc / 'bin/protoc',
                       Python_EXECUTABLE=shutil.which('python3'), FETCHCONTENT_TRY_FIND_PACKAGE_MODE='NEVER')
    args = [str(cmake), '-S', str(source / 'cmake'), '-B', str(work / 'build'), '-G', 'Ninja',
            *['-D' + k + '=' + str(v) for k, v in definitions.items()]]
    (work / 'configure-command.json').write_text(json.dumps(args, indent=2))
    (work / 'provenance.json').write_text(json.dumps(dict(source_commit=manifest['ort_source_commit'],
                                                       source_archive_sha256=hashlib.sha256(archive.read_bytes()).hexdigest(),
                                                       actual_nuttx_command=command,
                                                       target_executor_built=False), indent=2))
    print(shlex.join(args))


if __name__ == '__main__':
    main()
