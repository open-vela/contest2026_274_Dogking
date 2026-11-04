#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""New private replay kit with actual, hash-locked prior native I/O cases."""
import argparse,hashlib,json,re,shutil,tarfile
from pathlib import Path
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def need(ok,msg):
    if not ok:raise ValueError(msg)
def build(kit,report,target):
    need(not target.exists() and not Path(str(target)+'.tar.gz').exists(),'New paths required')
    trace=report/'tensor-trace.jsonl'
    need(sha(trace)=='40b2f7223c6da1804056ba3fde66e66036affa6d783b39af5b54e8630d5aa2ec','Prior native reference trace changed')
    rows=[json.loads(l) for l in trace.read_text().splitlines()];bytag={r['tag']:r for r in rows}
    enc=['wake-full-normalized-feature-offset-c%03d-encoder'%i for i in range(3)]
    dec=[];seen=set()
    for r in rows:
        if r['model']=='decoder' and tuple(r['inputs']) not in seen:
            seen.add(tuple(r['inputs']));dec.append(r['tag'])
            if len(dec)==3:break
    selected=[('encoder',tag) for tag in enc]+[('decoder',tag) for tag in dec]+[
        ('joiner','joiner-reload-a'),('joiner','wake-full-normalized-feature-offset-c000-f0-greedy-joiner'),
        ('joiner','wake-full-normalized-feature-offset-c000-f1-greedy-joiner'),('joiner','joiner-reload-a'),
        ('vocoder','vocoder-tts-0-0-0'),('vocoder','vocoder-tts-1-0-0'),
        ('vocoder','vocoder-silence-floor'),('vocoder','vocoder-tts-0-0-0')]
    need(len(dec)==3,'Need three distinct decoder contexts')
    lock=json.loads((kit/'kit-assets.json').read_text())
    for n,e in lock.items():need('/' not in n and (kit/n).is_file() and sha(kit/n)==e['sha256'],'Changed base replay kit')
    # Validate references before copying any private assets.
    cases=[]
    for index,(model,tag) in enumerate(selected):
        need(re.fullmatch('[a-zA-Z0-9_-]+',tag) and tag in bytag,'Unsafe/missing tag')
        row=bytag[tag];need(row['model']==model,'Wrong native model')
        ins=[];outs=[]
        for i,h in enumerate(row['inputs']):
            path=report/(tag+'-input')/('input%d.bin'%i);need(path.is_file() and not path.is_symlink() and sha(path)==h,'Changed input')
            ins.append(dict(path=path,bytes=path.stat().st_size,sha256=h))
        for i,h in enumerate(row['outputs']):
            path=report/tag/('output%d.bin'%i);need(path.is_file() and not path.is_symlink() and sha(path)==h,'Changed output')
            outs.append(dict(bytes=path.stat().st_size,sha256=h))
        cases.append(dict(id='case%02d'%index,model=model,origin=tag,inputs=ins,outputs=outs))
    for c,previous in zip(cases[1:3],cases[:2]):
        need(all(c['inputs'][i]['sha256']==previous['outputs'][i]['sha256'] for i in range(1,38)),'Native recurrent links mismatch')
    target.mkdir()
    for n in lock:shutil.copyfile(kit/n,target/n)
    (target/'tensor-check-linux-aarch64').chmod(0o700)
    for c in cases:
        folder=target/c['id'];folder.mkdir()
        for i,entry in enumerate(c['inputs']):
            shutil.copyfile(entry.pop('path'),folder/('input%d.bin'%i))
    (target/'dynamic-cases.json').write_text(json.dumps(dict(cases=cases,source_report=report.name,source_trace_sha256=sha(trace),file_based_diagnostic=True),indent=2))
    shutil.copyfile(Path(__file__).with_name('run-prepared-dynamic.py'),target/'run-prepared-dynamic.py')
    files={f.name:dict(bytes=f.stat().st_size,sha256=sha(f)) for f in target.iterdir() if f.is_file() and f.name!='kit-assets.json'}
    (target/'kit-assets.json').write_text(json.dumps(files,indent=2,sort_keys=True))
    archive=Path(str(target)+'.tar.gz')
    with tarfile.open(archive,'x:gz') as t:t.add(target,arcname=target.name)
    print(archive,sha(archive),len(cases),'actual native cases; no NPU calls')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('kit',type=Path);p.add_argument('report',type=Path);p.add_argument('target',type=Path);a=p.parse_args();build(a.kit,a.report,a.target)
