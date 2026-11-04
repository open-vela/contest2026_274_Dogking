#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""CPU-only numerical sensitivity probe. Never certifies ARM/NPU accuracy."""
import argparse, hashlib, json, math, struct, subprocess, zlib
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('batch',type=Path);p.add_argument('cases',type=Path)
p.add_argument('output',type=Path);a=p.parse_args()
repo=Path(__file__).resolve().parents[2]
pet=repo/'board/a733-cubie-a7z/openvela-overlay/apps/system/aipet'
knf=pet/'third_party/kaldi-native-fbank/csrc'
if a.output.exists():raise ValueError('Use a new diagnostic directory')
a.output.mkdir(parents=True)
cases=json.loads(a.cases.read_text())
flags={'unfused':['-ffp-contract=off']}
if ' fma ' in ' '+Path('/proc/cpuinfo').read_text().replace('\n',' ')+' ':
    flags['fused']=['-mfma','-ffp-contract=fast']
result=dict(cpu_only=True,arm_execution=False,npu_execution=False,acceptance=False,variants={})
for name,extra in flags.items():
    folder=a.output/name;folder.mkdir();exe=(folder/'export-fbank').resolve()
    cmd=['g++','-std=c++17','-Os',*extra,'-I'+str(pet),'-I'+str(knf/'include'),
         str(repo/'tools/npu-voice/export-fbank.cxx'),str(pet/'voice_fbank.cxx'),
         *map(str,sorted((knf/'src').glob('*.cc'))),'-lm','-o',str(exe)]
    subprocess.run(cmd,check=True)
    records=[]
    for i,c in enumerate(cases):
        source=a.batch/c['tag']/'speech'
        expected=(source/'features.f32').read_bytes()
        if hashlib.sha256(expected).hexdigest()!=c['feature_sha256']:
            raise ValueError('Reference provenance mismatch')
        output=folder/('features%d.f32'%i)
        subprocess.run([str(exe),str(source/'padded-s16le.pcm'),str(output)],check=True)
        actual=output.read_bytes()
        if len(actual)!=len(expected):raise ValueError('Feature size mismatch')
        old=struct.unpack('<%df'%(len(expected)//4),expected)
        new=struct.unpack('<%df'%(len(actual)//4),actual)
        if not all(map(math.isfinite,new)):raise ValueError('Nonfinite feature')
        error=[abs(x-y) for x,y in zip(old,new)]
        records.append(dict(case=i,crc='%08x'%((zlib.crc32(actual,0xffffffff)^0xffffffff)&0xffffffff),
                            differing=sum(x!=y for x,y in zip(old,new)),
                            maximum_absolute=max(error),rmse=math.sqrt(sum(e*e for e in error)/len(error))))
    result['variants'][name]=dict(flags=extra,cases=records)
(a.output/'summary.json').write_text(json.dumps(result,indent=2))
print(json.dumps(result,indent=2))
