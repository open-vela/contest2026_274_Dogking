#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Private six-fixture KWS kit: fresh A7TV encoder/decoder/joiner, no NBG loader."""
import argparse,hashlib,json,shutil,tarfile
from pathlib import Path
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def need(ok,msg):
    if not ok:raise ValueError(msg)
def build(replay,batch,search,old,new,target):
    need(not target.exists() and not Path(str(target)+'.tar.gz').exists(),'New target')
    lock=json.loads((replay/'kit-assets.json').read_text())
    for n,e in lock.items():need('/' not in n and sha(replay/n)==e['sha256'],'Replay kit changed')
    need(sha(search/'libaipet-kws-aarch64.so')=='c493b3d015168161e65542e9d1d697b730d53d7c04ddeab3ef1a6786eb5c6e27','Search library changed')
    # Hash oracles only. NO frozen logits/encoder outputs are put into this kit.
    oracles={};provenance=[]
    for report in (old,new):
        path=report/'tensor-trace.jsonl';provenance.append(dict(report=report.name,sha256=sha(path)))
        for line in path.read_text().splitlines():
            row=json.loads(line)
            if row['model'] not in ('encoder','decoder','joiner'):continue
            key=row['model']+':'+','.join(row['inputs'])
            need(key not in oracles or oracles[key]==row['outputs'],'Conflicting native reference')
            oracles[key]=row['outputs']
    cases=[c for c in json.loads((batch/'batch-fixtures/cases.json').read_text()) if c['scale']=='normalized']
    need(len(cases)==6,'Need six normalized real fixtures')
    for c in cases:need(sha(batch/c['feature_path'])==c['feature_sha256'],'Changed features')
    target.mkdir()
    for n in lock:shutil.copyfile(replay/n,target/n)
    (target/'tensor-check-linux-aarch64').chmod(0o700)
    source=Path(__file__).parent
    for n in ('run-prepared-kws.py','run-kws-search.py','stream-kws.py','joiner-fixtures.py','run-voice-batch.py','keyword-reference.py'):
        shutil.copyfile(source/n,target/n)
    for n in ('tokens.txt','keywords.txt','libaipet-kws-aarch64.so','kws-contract.json'):shutil.copyfile(search/n,target/n)
    for i,c in enumerate(cases):
        shutil.copyfile(batch/c['feature_path'],target/('features%d.f32'%i));c['kit_features']='features%d.f32'%i
    (target/'kws-cases.json').write_text(json.dumps(dict(cases=cases,oracles=oracles,provenance=provenance,precomputed_fbank=True),indent=2))
    files={f.name:dict(bytes=f.stat().st_size,sha256=sha(f)) for f in target.iterdir() if f.is_file() and f.name!='kit-assets.json'}
    (target/'kit-assets.json').write_text(json.dumps(files,indent=2,sort_keys=True))
    archive=Path(str(target)+'.tar.gz')
    with tarfile.open(archive,'x:gz') as t:t.add(target,arcname=target.name)
    print(archive,sha(archive),len(oracles),'native hash oracles')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('replay','batch','search','old','new','target'):p.add_argument(n,type=Path)
    a=p.parse_args();build(a.replay,a.batch,a.search,a.old,a.new,a.target)
