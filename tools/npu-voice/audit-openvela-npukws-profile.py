#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""v159 six-case supplied-log correctness + measured workload/latency audit.

File-feature measurements are NOT live microphone/ASR or sustained realtime
acceptance. Never contacts a board; no automatic resets/retries/capture.
"""
import argparse,hashlib,importlib.util,json,re
from pathlib import Path
s=importlib.util.spec_from_file_location('base_audit',Path(__file__).with_name('audit-openvela-npukws.py'))
base=importlib.util.module_from_spec(s);s.loader.exec_module(base)
POOLS=[8077312,606208,204800]
PHASES=['validate','rebuild_crc','map','clean','submit_wait','invalidate_check','commit','restore','total']
def require(value,message):
    if not value:raise ValueError(message)
def audit(expected,text,required_crc=None):
    result=base.audit(expected,text)
    policies=re.findall(r'npukws: cpu-policy=(auto|cpu6|cpu7) clean=(bounded|full) validation=all-native-CRCs',text)
    require(len(policies)==6,'Need six explicit CPU/cache policies')
    if required_crc is not None:
        modes=re.findall(r'validation=all-native-CRCs driver-crc=(slice8|legacy) app-crc=slice8',text)
        require(len(modes)==6 and all(m==required_crc for m in modes),'Missing/wrong v160 CRC policy')
    blocks=text.split('npukws: resident=3 cold-prepare-us=')[1:];cases=[]
    for index,(block,case,policy) in enumerate(zip(blocks,expected['cases'],policies)):
        chunks=12 if index<4 else 19
        timing=re.findall(r'npukws timing: chunks=(\d+) completed=(\d+) sum-us=(\d+) p50-us=(\d+) p95-us=(\d+) max-us=(\d+) over-160ms=(\d+) search-us=(\d+) affinity-restored=(\d+)',block)
        require(len(timing)==1,'Missing/duplicate chunk timing')
        n,completed,total,p50,p95,maximum,over,search,restored=map(int,timing[0])
        require(n==completed==chunks and restored==1,'Incomplete run or affinity restore failed')
        require(0<p50<=p95<=maximum<=total and 0<=over<=chunks,'Invalid timing statistics')
        require((maximum>160000)==(over>0),'Deadline counter differs from max')
        records=re.findall(r'npukws profile\[(\d+)\]: validate/rebuild/map/clean/hw/check/commit/restore/total-us=([0-9/]+) clean-bytes=(\d+) tables=(\d+) rebuilt=(\d+) app-check-us=(\d+)',block)
        require(len(records)==3 and [int(r[0]) for r in records]==[0,1,2],'Need three ordered model profiles')
        model_profiles=[];calls=[]
        for id,raw,clean,tables,rebuilt,app in records:
            id=int(id);values=list(map(int,raw.split('/')))
            clean,tables,rebuilt,app=map(int,[clean,tables,rebuilt,app])
            require(len(values)==9 and values[8]>=values[4],'Invalid driver timing')
            unit=24*1024*1024 if policy[1]=='full' else POOLS[id]
            require(clean>0 and clean%unit==0,'Unexpected clean range/bytes')
            count=clean//unit;calls.append(count)
            require(rebuilt==count*POOLS[id],'Working-image reconstruction differs')
            require(count*16384<=tables<=count*262144 and tables%16384==0,'Unexpected STLB range')
            if policy[1]=='full':require(tables==count*262144,'Full-clean comparison not full range')
            model_profiles.append(dict(model=['encoder','decoder','joiner'][id],calls=count,
                driver_us=dict(zip(PHASES,values)),clean_bytes=clean,table_bytes=tables,rebuild_bytes=rebuilt,application_check_us=app))
        require(calls[0]==chunks and 1<=calls[1]<=64 and sum(calls)==case['calls'],'Profile call count mismatch')
        cases.append(dict(case=index,tag=case['tag'],cpu_policy=policy[0],cache_policy=policy[1],models=model_profiles,
            chunk_sum_us=total,p50_us=p50,p95_us=p95,max_us=maximum,over_160ms=over,search_us=search,
            stride_budget_ratio=round(total/(chunks*160000),6),
            within_budget_for_this_file_only=(over==0 and total<=chunks*160000)))
    result.update(result='NPUKWS_PROFILE_SUPPLIED_LOGS_MATCH',cases=cases,sustained_realtime_accepted=False)
    return result
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('expected',type=Path);p.add_argument('transcript',type=Path)
    p.add_argument('--require-crc',choices=['slice8','legacy'],help='require explicit v160 policy on all six cases');a=p.parse_args()
    try:
        result=audit(json.loads(a.expected.read_text(encoding='utf-8')),a.transcript.read_text(encoding='utf-8-sig'),a.require_crc)
        result.update(expected_sha256=hashlib.sha256(a.expected.read_bytes()).hexdigest(),transcript_sha256=hashlib.sha256(a.transcript.read_bytes()).hexdigest())
        print(json.dumps(result,ensure_ascii=True,indent=2))
    except (ValueError,OSError,KeyError) as e:p.exit(2,'STOP: '+str(e)+'\n')
