#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Prepare PRIVATE, hash-locked candidate assets. No board/SD/UFS writes.

Not a generic model converter. Baker noncommercial terms apply. No model is
committed or uploaded. Destination is a NEW ignored out directory only.
"""
import argparse,hashlib,json,shutil
from pathlib import Path
REPO=Path(__file__).resolve().parents[2]
LOCK={
 'asr-encoder.a7tv':'d6470e919fbe06a94a08b0bc1887cc4c9e169c3859ba83890974250f0b160951',
 'asr-decoder.a7tv':'31e0a3b163b6b58735aa634d7e6c5a25071262d8bd7cd848fe1b98f5357adeea',
 'asr-joiner.a7tv':'f8231271d09b211bca752504f1a2d0da17456138ecf31475f3a9846c3af38044',
 'vocoder.a7tv':'0593a9d0f45ae4f637e74696b8acdda2d842b9531ade00e1bfdca58c45b9dad7',
 'model-steps-3.basic.ort':'cc2ec17c1bcca92239ed476d059900c8dac9c6137a444ce15ff5cc903845d243',
}
def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        while data:=f.read(1024*1024):h.update(data)
    return h.hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('new_output',type=Path)
    a=p.parse_args();out=a.new_output.resolve();assert out.is_relative_to(REPO/'out') and not out.exists()
    exported=REPO/'out/general-asr-export-v184-r2'
    index=json.loads((exported/'packages/export-index.json').read_text())
    sources={}
    for m in index['models']:
        name=m['package'];assert m['package_sha256']==LOCK[name]
        sources['general-asr/'+name]=(exported/'packages'/name,LOCK[name])
        role=m['model']
        for i,desc in enumerate(m['contract']['input']):
            source=exported/'fixture'/role/('input%d.bin'%i)
            assert source.stat().st_size==desc['bytes']
            sources['general-asr/fixture/'+role+'/'+source.name]=(source,digest(source))
    tokens=REPO/'out/a733-linux-general-asr-v183-r1/tokens.txt'
    sources['general-asr/tokens.txt']=(tokens,'a8e0e4ec53810e433789b54a5c0134a7eaa2ffca595a6334d54c00da858841d3')
    for name,source in [('vocoder.a7tv',REPO/'out/voice-prepared-v156-r1/vocoder.a7tv'),
                        ('model-steps-3.basic.ort',REPO/'out/matcha-ort-v183-r1/model-steps-3.basic.ort')]:
        sources['general-tts/'+name]=(source,LOCK[name])
    for name in ('tokens.txt','lexicon.txt'):
        source=REPO.parent/'references/matcha-icefall-zh-baker'/name
        sources['general-tts/'+name]=(source,digest(source))
    for name in ('LICENSE','ThirdPartyNotices.txt'):
        source=REPO/'out/ort-static-v185-r2'/name
        sources['general-tts/ORT-'+name]=(source,digest(source))
    # Audit every source before creating the destination.
    for source,expected in sources.values():assert digest(source)==expected,(source,'changed source')
    out.mkdir();records=[]
    for relative,(source,expected) in sorted(sources.items()):
        dest=out/relative;dest.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(source,dest);assert digest(dest)==expected
        records.append(dict(path=relative,bytes=dest.stat().st_size,sha256=expected))
    result=dict(schema=185,result='PRIVATE_CANDIDATE_ASSETS_NOT_BOARD_ACCEPTANCE',
        layout='copy general-asr and general-tts under /data/npu OR /ufs/npu',files=records,
        asr='general 6254-class Zipformer, NOT old KWS',acoustic='CPU Matcha default noise=1 length=1',
        vocoder='NPU VIP2 fixed F32 80x200 -> 51200 samples',tts_license='Baker noncommercial; do not redistribute as unrestricted',
        llm_included=False,credentials_included=False,hardware_executed=False,full_voice_chain=False)
    (out/'manifest.json').write_text(json.dumps(result,indent=2))
    print('Private assets:',len(records),'files;',sum(r['bytes'] for r in records),'bytes; no hardware/storage touched')
if __name__=='__main__':main()
