#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Relink a NEW diagnostic ELF with the real SDK link command and ORT archives.

Original ELF/bin/map stay unchanged. No image is packaged or board contacted.
This catches unresolved RTOS symbols/GOT/low-bank overflow before integration.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess

REPO=Path(__file__).resolve().parents[1]
SDK=(REPO/'../../quickly-openvela').resolve()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('sdk_build',type=Path);p.add_argument('static_prefix',type=Path)
    p.add_argument('new_output',type=Path);a=p.parse_args()
    build=a.sdk_build.resolve(strict=True);prefix=a.static_prefix.resolve(strict=True)
    out=a.new_output.resolve();assert out.is_relative_to(REPO/'out');out.mkdir()
    manifest=json.loads((prefix/'manifest.json').read_text())
    libs=[]
    for item in manifest['libraries']:
        path=prefix/item['path'];assert hashlib.sha256(path.read_bytes()).hexdigest()==item['sha256'];libs.append(str(path))
    ninja=SDK/'prebuilts/tools/ninja/bin/ninja'
    records=json.loads(subprocess.check_output([str(ninja),'-C',str(build),'-t','compdb','-x'],text=True))
    original=next(r for r in records if Path(r['output']).name=='nuttx')
    link=shlex.split(original['command'])
    start=next(i for i,x in enumerate(link) if Path(x).name in ('aarch64-none-elf-g++','aarch64-none-elf-gcc'))
    link=link[start:];link=link[:link.index('&&')] if '&&' in link else link
    assert '-o' in link and all(not x.startswith('@') for x in link)
    link[link.index('-o')+1]=str(out/'probe.elf')
    for i,x in enumerate(link):
        if '-Map=' in x or '--Map=' in x:link[i]=x.split('=',1)[0]+'='+str(out/'probe.map')
        if x in ('-Map','--Map') and i+1<len(link):link[i+1]=str(out/'probe.map')
        assert '--out-implib' not in x
    anchor=next(r for r in records if r['file'].endswith('/audio_app.cxx'))
    cmd=shlex.split(anchor['command'])
    if cmd[0]=='ccache':cmd=cmd[1:]
    for flag in ('-MF','-MT','-MQ'):
        if flag in cmd:
            i=cmd.index(flag);del cmd[i:i+2]
    cmd=[x for x in cmd if x not in ('-MD','-MMD')]
    cmd[cmd.index('-c')+1]=str(REPO/'tools/probe-ort-link-v184.cxx')
    obj=out/'probe.o';cmd[cmd.index('-o')+1]=str(obj)
    cmd.insert(1,'-I'+str(REPO/'board/a733-cubie-a7z/openvela-overlay/apps/system/aipet'))
    with (out/'compile.log').open('xb') as log:
        subprocess.run(cmd,cwd=anchor['directory'],stdout=log,stderr=subprocess.STDOUT,check=True)
    group=next(i for i,x in enumerate(link) if '--end-group' in x)
    link[group:group]=[str(obj),*libs]
    link.append('-Wl,-u,aipet_ort_link_probe')
    with (out/'link.log').open('xb') as log:
        rc=subprocess.run(link,cwd=original['directory'],stdout=log,stderr=subprocess.STDOUT,timeout=180).returncode
    result=dict(result='PROBE_LINKED_NOT_RUN' if not rc else 'PROBE_LINK_FAILED',status=rc,
        command=link,sdk_originals_modified=False,hardware_executed=False,models_loaded=False)
    if not rc:
        objcopy=SDK/'prebuilts/gcc/linux-x86_64/aarch64-none-elf/bin/aarch64-none-elf-objcopy'
        subprocess.run([str(objcopy),'-O','binary',str(out/'probe.elf'),str(out/'probe.bin')],check=True)
        with (out/'layout.log').open('xb') as log:
            env=dict(os.environ)
            env['PATH']=str(objcopy.parent)+os.pathsep+env.get('PATH','')
            result['layout_status']=subprocess.run(['bash',str(REPO/'tools/check-a733-elf-layout.sh'),
                str(out/'probe.elf'),str(out/'probe.bin')],stdout=log,stderr=subprocess.STDOUT,env=env).returncode
    (out/'summary.json').write_text(json.dumps(result,indent=2))
    print(result['result'],'link=',rc,'layout=',result.get('layout_status'))
    raise SystemExit(rc or result.get('layout_status',0))


if __name__=='__main__':main()
