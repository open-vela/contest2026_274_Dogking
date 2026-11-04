#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Private file-feature KWS fixtures from an actual prepared Linux report.

No models, audio or native tensors are committed to Git. CRC is a regression
check, NOT an authentication mechanism. Asset SHA256 manifest must be checked.
"""
import argparse, hashlib, json, shutil, struct, tarfile, zlib
from pathlib import Path

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def crc(p): return (zlib.crc32(p.read_bytes(), 0xffffffff)^0xffffffff)&0xffffffff
def build(kit, report, target, application_expected=None):
    if target.exists() or Path(str(target)+'.tar.gz').exists(): raise ValueError('Use NEW paths')
    summary=json.loads((report/'summary.json').read_text())
    if summary['result']!='A7TV_LINUX_KWS_SIX_FIXTURES_PASSED': raise ValueError('Unpassed source report')
    cases=json.loads((kit/'kws-cases.json').read_text())['cases']
    vocabulary={l.rsplit(maxsplit=1)[0]:int(l.rsplit(maxsplit=1)[1]) for l in (kit/'tokens.txt').read_text().splitlines()}
    words=[]
    for line in (kit/'keywords.txt').read_text().splitlines():
        phones,phrase=line.split('@');phones,threshold=phones.split('#');phones,boost=phones.split(':')
        ids=[vocabulary[t] for t in phones.split()];name=phrase.strip().encode()
        if not 1<=len(ids)<=16 or not 1<=len(name)<64: raise ValueError('Keyword bound')
        words.append(struct.pack('<I16iff64s',len(ids),*(ids+[0]*(16-len(ids))),float(boost),float(threshold),name))
    records={};mock=[];models=['encoder','decoder','joiner']
    for line in (report/'tensor-trace.jsonl').read_text().splitlines():
        entry=json.loads(line);n=entry['call'];id=models.index(entry['model'])
        inputs=[report/('call%05d-input'%n)/('input%d.bin'%i) for i in range(len(entry['inputs']))]
        outputs=[report/('call%05d-output'%n)/('output%d.bin'%i) for i in range(len(entry['outputs']))]
        if [sha(p) for p in inputs]!=entry['inputs'] or [sha(p) for p in outputs]!=entry['outputs']: raise ValueError('Native tensor changed')
        ins=[crc(p) for p in inputs];outs=[crc(p) for p in outputs];key=(id,*ins)
        identity=(tuple(entry['inputs']),tuple(entry['outputs']))
        if key in records and records[key][1]!=identity: raise ValueError('CRC collision or conflicting native data')
        records[key]=(struct.pack('<81I',id,len(ins),len(outs),*(ins+[0]*(39-len(ins))),*(outs+[0]*(39-len(outs)))),identity)
        mock.append(dict(model=id,inputs=ins,outputs=[str(p.resolve()) for p in outputs]))
    if len(records)>4096: raise ValueError('Reference bound')
    target.mkdir();voice=target/'voice';folder=voice/'kws';folder.mkdir(parents=True)
    index={m['model']:m for m in json.loads((kit/'export-index.json').read_text())['models']}
    for name in models:
        src=kit/(name+'.a7tv')
        if sha(src)!=index[name]['package_sha256']: raise ValueError('Prepared package changed')
        shutil.copyfile(src,voice/src.name)
    (folder/'words.bin').write_bytes(struct.pack('<4I',0x574b3741,1,len(words),0)+b''.join(words))
    (folder/'reference.bin').write_bytes(struct.pack('<4I',0x524b3741,1,len(records),0)+b''.join(r[0] for r in records.values()))
    for c in cases:
        src=kit/c['kit_features']
        if sha(src)!=c['feature_sha256']: raise ValueError('Features changed')
        shutil.copyfile(src,folder/src.name)
    (folder/'cases.json').write_text(json.dumps(cases,indent=2))
    # Host-only mapping: never copied into the board fixture pack.
    (target/'host-native-map.json').write_text(json.dumps(mock,indent=2))
    commands=['File-feature checkpoint only. No microphone/cloud/ASR/TTS/Agent actions.',
              'Use the v158 experimental candidate. STOP on error; do not kill/reset/retry.',
              'Before and after each case: npu info / free. Three models are released on return.',
              'Install voice/kws as /data/npu/voice/kws; encoder/decoder/joiner unchanged from v156.',
              'Verify all SHA256 values before installation; CRC is not authentication.','']
    commands += ['aipet audio npukws /data/npu/voice /data/npu/voice/kws/'+c['kit_features'] for c in cases]
    extra=[]
    if application_expected:
        expected=json.loads(application_expected.read_text())
        if not expected.get('host_file_oracle_only') or len(expected['cases'])!=6:raise ValueError('Invalid host application oracle')
        for c,e in zip(cases,expected['cases']):
            if c['tag']!=e['tag'] or ((c['expected'] and c['expected'] not in e['names']) or (not c['expected'] and e['names'])):raise ValueError('Host keyword mismatch')
        shutil.copyfile(application_expected,target/'application-expected.json');extra=['application-expected.json']
        commands+=['','Expected (host-file-oracle only; compare actual board logs):']
        commands+=['case=%d calls=%d hits=%d chain=%s names=%s'%(e['case'],e['calls'],e['hits'],e['chain'],','.join(e['names'])) for e in expected['cases']]
    (target/'TEST_COMMANDS.txt').write_text('\n'.join(commands)+'\n')
    checked=[p for p in voice.rglob('*') if p.is_file()]+[target/n for n in extra]
    (target/'assets-sha256.json').write_text(json.dumps({str(p.relative_to(target)):dict(bytes=p.stat().st_size,sha256=sha(p)) for p in checked},indent=2))
    archive=Path(str(target)+'.tar.gz')
    with tarfile.open(archive,'x:gz') as t:
        for name in ['voice','assets-sha256.json','TEST_COMMANDS.txt']+extra: t.add(target/name,arcname=target.name+'/'+name)
    print('references=%d cases=%d archive=%s sha256=%s'%(len(records),len(cases),archive,sha(archive)))
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ['kit','report','target']:p.add_argument(n,type=Path)
    p.add_argument('--application-expected',type=Path)
    a=p.parse_args();build(a.kit.resolve(),a.report.resolve(),a.target.resolve(),a.application_expected)
