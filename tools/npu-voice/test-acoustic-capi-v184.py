#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile/run portable C++ frontend+CPU acoustic adapter on Linux host only."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

REPO=Path(__file__).resolve().parents[2]


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('new_output',type=Path)
    a=p.parse_args();out=a.new_output.resolve();assert out.is_relative_to(REPO/'out');out.mkdir()
    lib=REPO/'out/ort-capi-host-v184-r1'
    metadata=json.loads((lib/'provenance.json').read_text())
    for item in metadata['files']:
        assert hashlib.sha256((lib/item['file']).read_bytes()).hexdigest()==item['sha256']
    soname=lib/'libonnxruntime.so.1'
    if not soname.exists():soname.symlink_to('libonnxruntime.so.1.23.2')
    pet=REPO/'board/a733-cubie-a7z/openvela-overlay/apps/system/aipet'
    cmd=['g++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror',
         '-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(pet),
         str(REPO/'tools/test-acoustic-capi-v184.cxx'),str(pet/'voice_acoustic.cxx'),
         str(pet/'voice_tts_frontend.cxx'),str(lib/'libonnxruntime.so.1.23.2'),
         '-Wl,-rpath,'+str(lib),'-pthread','-o',str(out/'test')]
    with (out/'compile.log').open('xb') as log:
        subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True)
    args=[str(out/'test'),str(REPO/'out/matcha-ort-v183-r1/model-steps-3.basic.ort'),
          str(REPO.parent/'references/matcha-icefall-zh-baker'),str(out),'CPU_ONLY']
    with (out/'run.log').open('xb') as log:
        subprocess.run(args,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
    (out/'summary.json').write_text(json.dumps(dict(result='HOST_CPP_CPU_ACOUSTIC_EXECUTED',
        target_runtime=False,npu_executed=False,full_voice_chain=False,command=cmd,
        sources={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (pet/'voice_acoustic.cxx',pet/'voice_tts_frontend.cxx')}),indent=2))
    print((out/'run.log').read_text())


if __name__=='__main__':main()
