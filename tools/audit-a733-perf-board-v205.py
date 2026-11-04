#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Offline v205 sampled-log accounting; never proves visuals or authentic HW."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,re
s=importlib.util.spec_from_file_location('v204',Path(__file__).with_name('audit-a733-live-board-v204.py'))
old=importlib.util.module_from_spec(s);s.loader.exec_module(old)
STATUS=re.compile(old.STATUS.pattern.replace('v204','v20[56789]'))
INFO=re.compile(r'live205: optimized=([01]) sensor-stream=([01]) setup-us=(\d+) loop-us=(\d+) warm-us=(\d+) warm-fps-milli=(\d+) inference-fps-milli=(\d+) warm-npu-runs=(\d+)')
VALIDATION=re.compile(r'live205 validation: requested=([01]) verified=([01]) run-ioctls=(\d+);')
PARTS=re.compile(r'live205 npu seq=(\d+) hit=([01]) retained=(\d+) prep=(\d+) package=(\d+) map=(\d+) input=(\d+) sync=(\d+) hw=(\d+) output=(\d+) decode=(\d+) restore=(\d+) total=(\d+) us')
VERIFY=re.compile(r'live205 verify phase=(cold|warm) input=([0-9a-f]{8}) outputs=([0-9a-f]{8}(?:/[0-9a-f]{8}){5}) hit=([01]) retained=(\d+) equal-legacy=([01])')
ANSI=re.compile(r'\x1b\[[0-?]*[ -/]*[@-~]')
def audit(text,index=-1):
    runs=[]
    for line in ANSI.sub('',text).splitlines():
        a=old.ACCEPT.search(line)
        if a:runs.append(dict(seconds=int(a[1]),every=int(a[2]),lines=[]))
        if runs:runs[-1]['lines'].append(line)
    if not runs:raise ValueError('Missing accepted line; do not invent a session from garbled text')
    run=runs[index];frames={};outputs={};boxes={};parts={};verify={};final=info=validation=None;totals={}
    if not 1<=run['seconds']<=3600 or not 0<=run['every']<=60:raise ValueError('Invalid declared bounds')
    for line in run['lines']:
        a=old.FRAME.search(line)
        if a:
            seq=int(a[1]);infer=int(a[4]);n=int(a[3])
            if not 1<=seq<=32768 or seq in frames or (frames and seq<=max(frames)):raise ValueError('Duplicate/out-of-order sampled frame')
            if infer!=bool(run['every'] and (seq-1)%run['every']==0) or n>7 or (not infer and n):raise ValueError('Cadence/box mismatch')
            frames[seq]=dict(input=a[2],boxes=n,inferred=infer,phase_us=list(map(int,a.group(5,6,7,8))))
        a=old.NPU.search(line)
        if a:
            seq=int(a[1])
            if seq not in frames or seq!=max(frames) or not frames[seq]['inferred'] or seq in outputs:raise ValueError('Unbound sampled NPU output')
            outputs[seq]=a[2].split('/');boxes[seq]=[]
        a=old.DET.search(line)
        if a:
            seq,cls,score,x1,y1,x2,y2=map(int,a.groups())
            if seq not in outputs or seq!=max(frames) or not (0<=cls<=65535 and score<=1000 and 0<=x1<x2<=640 and 0<=y1<y2<=640):raise ValueError('Unbound/invalid box')
            boxes[seq].append([cls,score,x1,y1,x2,y2])
        a=PARTS.search(line)
        if a:
            seq=int(a[1]);v=list(map(int,a.groups()[1:]));hit,retained,prep,package,mapping,input_us,sync,hw,output,decode,restore,total=v
            if seq not in outputs or seq in parts or retained>16:raise ValueError('Unbound/invalid NPU profile')
            if package+mapping+input_us+sync>prep or prep+hw+output+decode+restore>total:raise ValueError('Overlapping/impossible NPU subdivisions')
            parts[seq]=dict(hit=hit,retained=retained,prepare_us=prep,hw_exec_us=hw,total_us=total)
        a=VERIFY.search(line)
        if a:
            if a[1] in verify:raise ValueError('Duplicate verification phase')
            verify[a[1]]=dict(input=a[2],outputs=a[3].split('/'),hit=int(a[4]),retained=int(a[5]),equal=bool(int(a[6])))
        a=STATUS.search(line)
        if a and a[1]=='0':final=a.groups()
        a=INFO.search(line)
        if a:info=list(map(int,a.groups()))
        a=VALIDATION.search(line)
        if a:validation=list(map(int,a.groups()))
        if line.startswith('live205 NPU-us:'):totals={k:int(v) for k,v in re.findall(r'([a-z-]+)=(\d+)',line)}
    if not final or info is None or validation is None:raise ValueError('Missing inactive final/profile/validation summaries')
    _,stop,stage,error,cleanup,count,inferred,elapsed,fps,last=final
    stop,error,cleanup,count,inferred,elapsed=map(int,(stop,error,cleanup,count,inferred,elapsed))
    if not 0<=count<=32768 or elapsed<=0:raise ValueError('Invalid final counters')
    expected=(1+(count-1)//run['every']) if count and run['every'] else 0
    if inferred!=expected or any(seq>count for seq in frames):raise ValueError('Final cadence/frame count mismatch')
    if set(outputs)!={seq for seq,f in frames.items() if f['inferred']}:raise ValueError('Missing sampled output records (not all frames are logged)')
    for seq in outputs:
        if len(boxes[seq])!=frames[seq]['boxes']:raise ValueError('Incomplete sampled box list')
    if count in frames and frames[count]['input']!=last:raise ValueError('Last sampled/final input CRC differs')
    optimized,streaming,setup_us,loop_us,warm_us,warm_fps,infer_fps,warm_runs=info
    requested,verified,calls=validation
    clean=stage=='complete' and error==cleanup==0
    if optimized and set(parts)!=set(outputs):raise ValueError('Missing sampled resident profile')
    if verified:
        if not requested or set(verify)!={'cold','warm'} or not all(v['equal'] for v in verify.values()):raise ValueError('Claimed verification lacks full successful evidence')
        cold,hot=verify['cold'],verify['warm']
        if cold['hit']!=0 or hot['hit']!=1 or cold['input']!=hot['input'] or cold['outputs']!=hot['outputs']:raise ValueError('Cold/warm input/output/hit mismatch')
    if clean and not stop:
        if requested and inferred and not verified:raise ValueError('Unverified requested run')
        if calls!=inferred+(2 if verified else 0):raise ValueError('NPU calls/displayed inference count mismatch')
    if warm_runs>inferred or loop_us>1000*(elapsed+1):raise ValueError('Impossible aggregate timing/count')
    recorded=int(fps.replace('.',''));hi=count*1000000//elapsed;lo=count*1000000//(elapsed+1)
    if clean and not lo<=recorded<=hi:raise ValueError('FPS inconsistent with underlying time rounding')
    if count>1 and warm_us and warm_fps!=(count-1)*1000000000//warm_us:raise ValueError('Warm FPS mismatch')
    if loop_us and infer_fps!=inferred*1000000000//loop_us:raise ValueError('Inference FPS mismatch')
    return dict(result='V205_SAMPLED_SERIAL_ACCOUNTING_REPORT',clean_exit=clean,stop=stop,error=error,cleanup=cleanup,
      requested_seconds=run['seconds'],detect_every=run['every'],declared_frames=count,declared_inferences=inferred,
      sampled_frames=len(frames),sampled_npu=len(outputs),sampled_input_crc_distinct=len({x['input'] for x in frames.values()}),
      optimized=optimized,sensor_stream=streaming,setup_us=setup_us,loop_us=loop_us,warm_us=warm_us,
      warm_fps_milli=warm_fps,inference_fps_milli=infer_fps,npu_run_ioctls=calls,verified_same_input=bool(verified),
      npu_us=totals,sampled_profiles=parts,hardware_verified=False,visuals_verified=False,
      note='Counters are declared by firmware; sampled lines are not total frames. hw-exec includes init/wait. Verify extra runs are not inference display frames.')
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path);p.add_argument('--run-index',type=int,default=-1);p.add_argument('--output',type=Path)
    a=p.parse_args();raw=a.log.read_bytes();result=audit(raw.decode('utf-8',errors='replace'),a.run_index);result['serial_sha256']=hashlib.sha256(raw).hexdigest()
    value=json.dumps(result,indent=2)+'\n'
    if a.output:
        with a.output.open('x') as f:f.write(value)
    print(value,end='')
if __name__=='__main__':main()
