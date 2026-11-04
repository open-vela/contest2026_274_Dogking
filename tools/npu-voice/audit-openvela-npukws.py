#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Audit supplied six-case logs, never access a board or claim provenance."""
import argparse,hashlib,json,re
from pathlib import Path

def audit(expected,text):
    if re.search(r'FAULT cpu=|npukws: status=-',text):raise ValueError('Failure in transcript')
    blocks=text.split('npukws: resident=3 cold-prepare-us=')[1:]
    if len(blocks)!=6:raise ValueError('Need exactly six resident fixture runs in order')
    for b,c in zip(blocks,expected['cases']):
        ends=re.findall(r'npukws: status=(\d+) calls=(\d+) hits=(\d+) chain=([0-9a-f]{8}).*; resident released',b)
        if len(ends)!=1:raise ValueError('Missing/duplicate completion')
        status,calls,hits,chain=ends[0]
        names=re.findall(r'npukws hit: word=\d+ name=(\S+)',b)
        if status!='0' or (int(calls),int(hits),chain,names)!=(c['calls'],c['hits'],c['chain'],c['names']):
            raise ValueError('Fixture differs: '+c['tag'])
    return dict(result='NPUKWS_SUPPLIED_LOGS_MATCH',cases=6,
                calls=sum(c['calls'] for c in expected['cases']),log_only=True,
                live_microphone=False,asr=False,end_to_end_voice_assistant=False)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('expected',type=Path);p.add_argument('transcript',type=Path);a=p.parse_args()
    try:
        result=audit(json.loads(a.expected.read_text(encoding='utf-8')),a.transcript.read_text(encoding='utf-8-sig'))
        result.update(expected_sha256=hashlib.sha256(a.expected.read_bytes()).hexdigest(),
                      transcript_sha256=hashlib.sha256(a.transcript.read_bytes()).hexdigest())
        print(json.dumps(result,indent=2))
    except (OSError,ValueError,KeyError) as e:p.exit(2,'STOP: '+str(e)+'\n')
