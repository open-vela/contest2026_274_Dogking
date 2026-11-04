#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Explicit bounded Linux VIP2 owned-buffer replay; no reset/retry/MMIO writes."""
import argparse,glob,hashlib,json,os,platform,shutil,subprocess,tarfile,tempfile,time
from pathlib import Path
def require(ok,message):
    if not ok:raise ValueError(message)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def check(base):
    index=json.loads((base/'export-index.json').read_text())
    require(index['cid']=='1000003b' and {m['model'] for m in index['models']}=={'encoder','decoder','joiner','vocoder'},'Bad model index')
    lock=json.loads((base/'kit-assets.json').read_text())
    require({'tensor-check-linux-aarch64','export-index.json','run-prepared-replay.py'}<=set(lock),'Missing locked components')
    for name,entry in lock.items():
        p=base/name;require('/' not in name and not p.is_symlink() and p.is_file() and p.stat().st_size==entry['bytes'] and sha(p)==entry['sha256'],'Asset changed: '+name)
    for m in index['models']:
        p=base/m['package'];require(p.name==m['model']+'.a7tv' and p.stat().st_size==m['package_bytes'] and sha(p)==m['package_sha256'],'Package changed')
    return {m['model']:m for m in index['models']}
def temperatures():
    values={}
    for p in glob.glob('/sys/class/thermal/thermal_zone*/temp'):
        try:values[p]=int(Path(p).read_text())
        except (OSError,ValueError):pass
    require(values and max(values.values())<75000,'Missing/high thermal evidence')
    return values
def run(base,allocate):
    models=check(base)
    require(platform.machine()=='aarch64' and os.geteuid()==0 and Path('/dev/vipcore').is_char_device(),'Use physical A733 Linux via sudo')
    owners=[p for p in glob.glob('/proc/[0-9]*/fd/*') if os.path.realpath(p)=='/dev/vipcore']
    require(not owners,'Other NPU owners: '+repr(owners))
    require(shutil.disk_usage(base).free>2*1024**3,'Need 2 GiB free')
    work=Path(tempfile.mkdtemp(prefix='prepared-replay-',dir=base));stages=[];result='FAILED';error=None
    deadline=time.monotonic()+600
    try:
        for name in ('joiner','decoder','encoder','vocoder'):
            temp=temperatures();m=models[name];log=work/(name+'.log')
            cmd=[str(base/'tensor-check-linux-aarch64'),str(base/m['package'])]
            if allocate:cmd+=['--allocate']
            else:cmd+=['--run',str(m['command']['native_command_bytes']),'-',str(work/(name+'-output'))]
            require(time.monotonic()<deadline,'Total deadline')
            print(('ALLOCATE ' if allocate else 'REPLAY ')+name,flush=True)
            with log.open('xb') as stream:
                p=subprocess.run(['timeout','--signal=TERM','--kill-after=5s','60s',*cmd],stdout=stream,stderr=subprocess.STDOUT,timeout=66)
            require(p.returncode==0,'Stage failed: '+name+' exit='+str(p.returncode)+'; stop, no retry/reset')
            row=dict(model=name,temperature_before=temp,process_exit=0)
            if not allocate:
                outputs=[]
                for i,expected in enumerate(m['outputs']):
                    path=work/(name+'-output')/('output%d.bin'%i)
                    require(path.is_file() and not path.is_symlink() and path.stat().st_size==expected['bytes'] and sha(path)==expected['sha256'],'Golden mismatch '+name+':'+str(i))
                    outputs.append(dict(index=i,bytes=path.stat().st_size,sha256=sha(path)))
                row['outputs']=outputs
            stages.append(row);print(name+' passed',flush=True)
        result='EXACT_VIP_ALLOCATION_PASSED' if allocate else 'A7TV_LINUX_FIXED_GOLDENS_PASSED'
    except (ValueError,OSError,subprocess.SubprocessError) as e:
        error=str(e);print('STOP:',error,flush=True)
    finally:
        (work/'summary.json').write_text(json.dumps(dict(result=result,error=error,stages=stages,allocation_only=allocate,openvela_executed=False,dynamic_input_verified=False),indent=2))
        archive=base/(work.name+'.tar.gz')
        with tarfile.open(archive,'x:gz') as tar:tar.add(work,arcname=work.name)
        if os.environ.get('SUDO_UID','').isdigit() and os.environ.get('SUDO_GID','').isdigit():os.chown(archive,int(os.environ['SUDO_UID']),int(os.environ['SUDO_GID']))
        print('REPORT:',archive,flush=True);print('RESULT:',result,flush=True)
    return 0 if error is None else 2
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('mode',choices=['check','allocate','run']);p.add_argument('--npu-idle-confirmed',action='store_true');a=p.parse_args()
    try:
        base=Path(__file__).resolve().parent
        if a.mode=='check':check(base);print('All prepared replay assets verified; no NPU calls')
        else:
            require(a.npu_idle_confirmed,'Explicit idle confirmation required');raise SystemExit(run(base,a.mode=='allocate'))
    except (OSError,ValueError,KeyError) as e:p.exit(2,'STOP: '+str(e)+'\n')
