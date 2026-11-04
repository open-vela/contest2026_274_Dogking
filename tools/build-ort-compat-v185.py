#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile genuine NuttX libc++ filesystem sources omitted by its C++17 CMake.

Uses the exact saved libc++ target command, no SDK modifications. Includes an
exception-count implementation ONLY for exception-disabled ABI. No Linux code.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess

REPO=Path(__file__).resolve().parents[1]
SDK=(REPO/'../../quickly-openvela').resolve()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('sdk_build',type=Path);p.add_argument('new_output',type=Path);a=p.parse_args()
    build=a.sdk_build.resolve(strict=True);out=a.new_output.resolve();assert out.is_relative_to(REPO/'out');out.mkdir()
    config=(build/'.config').read_text()
    assert 'CONFIG_CXX_STANDARD="gnu++17"' in config and '# CONFIG_CXX_EXCEPTION is not set' in config
    ninja=SDK/'prebuilts/tools/ninja/bin/ninja'
    records=json.loads(subprocess.check_output([str(ninja),'-C',str(build),'-t','compdb','-x'],text=True))
    anchor=next(r for r in records if r['file'].endswith('/libcxx/src/system_error.cpp'))
    command=shlex.split(anchor['command'])
    if command[0]=='ccache':command=command[1:]
    for flag in ('-MF','-MT','-MQ'):
        if flag in command:
            i=command.index(flag);del command[i:i+2]
    command=[x for x in command if x not in ('-MD','-MMD')]
    fs=SDK/'nuttx/libs/libxx/libcxx/libcxx/src/filesystem'
    sources=[*sorted(fs.glob('*.cpp')),REPO/'tools/ort-noexceptions-v185.cxx']
    assert len(sources)==8
    objects=[];evidence=[]
    for source in sources:
        obj=out/(source.name+'.o');cmd=command.copy()
        cmd[cmd.index('-c')+1]=str(source);cmd[cmd.index('-o')+1]=str(obj)
        with (out/(source.name+'.log')).open('xb') as log:
            rc=subprocess.run(cmd,cwd=anchor['directory'],stdout=log,stderr=subprocess.STDOUT).returncode
        evidence.append(dict(source=str(source),sha256=hashlib.sha256(source.read_bytes()).hexdigest(),status=rc,command=cmd))
        print(source.name,'status=',rc,flush=True)
        if rc:
            (out/'summary.json').write_text(json.dumps(dict(result='COMPAT_COMPILE_FAILED',sources=evidence),indent=2))
            raise SystemExit(rc)
        objects.append(str(obj))
    ar=SDK/'prebuilts/gcc/linux-x86_64/aarch64-none-elf/bin/aarch64-none-elf-ar'
    subprocess.run([str(ar),'rcs',str(out/'liba733_ort_nuttx_compat.a'),*objects],check=True)
    (out/'summary.json').write_text(json.dumps(dict(result='REAL_NUTTX_COMPAT_COMPILED_NOT_LINKED',sources=evidence,
        exception_profile='disabled; uncaught count is zero by definition',sdk_modified=False,hardware_executed=False),indent=2))


if __name__=='__main__':main()
