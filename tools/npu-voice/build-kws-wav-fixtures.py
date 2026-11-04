#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Wrap verified, already-padded private S16 fixtures as canonical WAV; no NPU."""
import argparse,hashlib,json,tarfile,wave,zlib
from pathlib import Path
def sha(data):return hashlib.sha256(data).hexdigest()
p=argparse.ArgumentParser(description=__doc__);p.add_argument('batch',type=Path);p.add_argument('cases',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
if a.output.exists() or Path(str(a.output)+'.tar.gz').exists():raise ValueError('Use NEW output')
cases=json.loads(a.cases.read_text(encoding='utf-8'));verified=[]
for c in cases:
    base=a.batch/c['tag'];m=json.loads((base/'manifest.json').read_text())
    raw=(base/'speech/padded-s16le.pcm').read_bytes();f=(base/'speech/features.f32').read_bytes()
    if m['normalization']!='S16 / 32768' or sha(raw)!=m['cases']['speech']['pcm_sha256'] or sha(f)!=c['feature_sha256'] or len(raw)%2 or not 16000<=len(raw)<=320000:raise ValueError('Fixture/provenance mismatch '+c['tag'])
    verified.append((c,raw,f))
a.output.mkdir();folder=a.output/'voice/kws/wav';folder.mkdir(parents=True);records=[]
for i,(c,raw,features) in enumerate(verified):
    path=folder/('wave%d.wav'%i)
    with wave.open(str(path),'wb') as w:w.setparams((1,2,16000,0,'NONE','not compressed'));w.writeframes(raw)
    records.append(dict(case=i,path=str(path.relative_to(a.output)),bytes=path.stat().st_size,sha256=sha(path.read_bytes()),pcm_sha256=sha(raw),feature_sha256=c['feature_sha256'],feature_crc32='%08x'%((zlib.crc32(features,0xffffffff)^0xffffffff)&0xffffffff),frames=c['frames'],keyword=c['expected']))
manifest=dict(schema=1,source='previous Linux-validated normalized fixtures, includes existing 5120-sample silence tail',models_included=False,hardware_executed=False,cases=records)
(a.output/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=True,indent=2))
with tarfile.open(str(a.output)+'.tar.gz','x:gz') as archive:archive.add(a.output,arcname=a.output.name)
print(json.dumps(manifest,ensure_ascii=True,indent=2))
