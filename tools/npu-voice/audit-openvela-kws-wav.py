#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Six supplied WAV-fixture runs, NOT arbitrary/live microphone accuracy proof."""
import argparse,hashlib,importlib.util,json,re
from pathlib import Path
spec=importlib.util.spec_from_file_location('profile',Path(__file__).with_name('audit-openvela-npukws-profile.py'));profile=importlib.util.module_from_spec(spec);spec.loader.exec_module(profile)
END=r'npukws-input: status=(-?\d+) calls=(\d+) candidates=(\d+) known=(\d+) unknown=(\d+) chain=([0-9a-f]{8}) feature-crc=([0-9a-f]{8}) frames=(\d+) unused-tail=(\d+) elapsed-us=(\d+); resident released; acoustic accuracy NOT accepted'
def audit(expected,manifest,text):
    rows=re.findall(END,text);front=re.findall(r'npukws-input: buffered frontend PCM=16000/mono/S16 frames=(\d+) bins=80; capture already stopped; NOT live streaming',text)
    profile.require(len(rows)==len(front)==len(manifest['cases'])==6,'Need exactly six WAV frontend/completion pairs')
    for i,(row,w) in enumerate(zip(rows,manifest['cases'])):
        status,calls,hits,known,unknown,chain,crc,frames,tail,elapsed=row
        profile.require(status=='0' and int(known)==int(calls) and unknown=='0','Unknown/failed WAV cannot pass golden acceptance')
        n=int(frames);chunks=(n-29)//16+1
        profile.require(w['case']==i and n==w['frames']==int(front[i]) and crc==w['feature_crc32'] and int(tail)==n-(29+(chunks-1)*16),'Frontend bytes/frames/tail differ')
    def compatible(m):
        r=m.groups();return 'npukws: status=%s calls=%s hits=%s chain=%s elapsed-us=%s; resident released'%(r[0],r[1],r[2],r[5],r[9])
    # Only after requiring all calls have real golden matches, reuse strict
    # calls/hits/chain/byte-range/timing verification. Original log untouched.
    view=re.sub(END,compatible,text).replace('npukws-input candidate:','npukws hit:')
    result=profile.audit(expected,view,'slice8')
    result.update(result='KWS_WAV_SIX_GOLDEN_SUPPLIED_LOGS_MATCH',wav_frontend=True,live_microphone=False,arbitrary_audio_accuracy=False)
    return result
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ['expected','manifest','transcript']:p.add_argument(name,type=Path)
    a=p.parse_args()
    try:
        result=audit(json.loads(a.expected.read_text(encoding='utf-8')),json.loads(a.manifest.read_text(encoding='utf-8')),a.transcript.read_text(encoding='utf-8-sig'))
        result['transcript_sha256']=hashlib.sha256(a.transcript.read_bytes()).hexdigest();print(json.dumps(result,indent=2))
    except (ValueError,KeyError,OSError) as e:p.exit(2,'STOP: '+str(e)+'\n')
