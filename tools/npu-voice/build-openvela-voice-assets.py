#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Private /data/npu/voice files + exact CRC oracles for explicit RTOS tests."""
import argparse,hashlib,json,shutil,tarfile,zlib
from pathlib import Path
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def copy(source,dest):
    if not source.is_file() or source.is_symlink():raise ValueError('Nonregular source')
    dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,dest)
def build(prepared,layout,dynamic,reference,target):
    if target.exists() or Path(str(target)+'.tar.gz').exists():raise ValueError('New output paths required')
    index=json.loads((prepared/'export-index.json').read_text());fixed=json.loads((layout/'layout-fixtures/index.json').read_text())['models']
    cases=json.loads((dynamic/'dynamic-cases.json').read_text())['cases'];models={m['model']:m for m in index['models']}
    target.mkdir();root=target/'voice';root.mkdir();oracles=[]
    for name,m in models.items():
        source=prepared/m['package']
        if sha(source)!=m['package_sha256']:raise ValueError('Package hash changed')
        copy(source,root/source.name)
        for i,e in enumerate(fixed[name]['inputs']):
            p=layout/e['path']
            if sha(p)!=e['sha256']:raise ValueError('Fixed input changed')
            copy(p,root/'input'/name/('input%d.bin'%i))
        oracles.append(dict(model=name,case='fixed',input_dir='input/'+name,origin=fixed[name]['origin'],outputs=m['outputs']))
    for c in cases:
        for i,e in enumerate(c['inputs']):
            p=dynamic/c['id']/('input%d.bin'%i)
            if sha(p)!=e['sha256']:raise ValueError('Dynamic input changed')
            copy(p,root/'dynamic'/c['id']/p.name)
        oracles.append(dict(model=c['model'],case=c['id'],input_dir='dynamic/'+c['id'],origin=c['origin'],outputs=c['outputs']))
    for c in oracles:
        for i,e in enumerate(c['outputs']):
            p=reference/c['origin']/('output%d.bin'%i)
            if sha(p)!=e['sha256']:raise ValueError('Native oracle changed')
            e['raw_crc32']='%08x'%((zlib.crc32(p.read_bytes(),0xffffffff)^0xffffffff)&0xffffffff)
    (root/'expected-outputs.json').write_text(json.dumps(dict(cases=oracles,openvela_board_passed=False,not_asr=True),indent=2))
    commands=['Install the voice directory as /data/npu/voice on the OPENVELA data partition.',
      'Do not install/flash anything on the currently running Linux card.',
      'Use the separately built experimental kernel. No automatic inference at boot.',
      'Run joiner, decoder, encoder, vocoder fixed checkpoints first; STOP on any failure.',
      'Compare every output CRC with voice/expected-outputs.json.',
      'Do NOT Ctrl+C/task_delete during a call; deadline is 5 seconds. Do NOT retry hardware failures.',
      'This is native tensor validation, NOT ASR or a working complete local voice assistant.','']
    for name in ('joiner','decoder','encoder','vocoder'):
        commands.append('aipet audio npuref %s /data/npu/voice/%s.a7tv /data/npu/voice/input/%s'%(name,name,name))
    commands+=['','Only after fixed cases pass; dynamic outputs need manual CRC comparison:']
    commands+=['aipet audio npuref %s /data/npu/voice/%s.a7tv /data/npu/voice/dynamic/%s dynamic'%(c['model'],c['model'],c['id']) for c in cases]
    (target/'TEST_COMMANDS.txt').write_text('\n'.join(commands)+'\n')
    files={str(p.relative_to(target)):dict(bytes=p.stat().st_size,sha256=sha(p)) for p in target.rglob('*') if p.is_file()}
    (target/'assets-sha256.json').write_text(json.dumps(files,indent=2,sort_keys=True))
    archive=Path(str(target)+'.tar.gz')
    with tarfile.open(archive,'x:gz') as t:t.add(target,arcname=target.name)
    print(archive,archive.stat().st_size,sha(archive),len(oracles),'checkpoint cases')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('prepared','layout','dynamic','reference','target'):p.add_argument(n,type=Path)
    a=p.parse_args();build(a.prepared,a.layout,a.dynamic,a.reference,a.target)
