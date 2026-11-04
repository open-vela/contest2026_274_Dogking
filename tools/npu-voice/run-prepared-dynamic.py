#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Actual varying native inputs + encoder state feedback into fixed A7TV maps.

Process/file-based diagnostic, not a realtime RAM-persistent application.
"""
import glob,importlib.util,json,os,platform,shutil,subprocess,tarfile,tempfile,time
from pathlib import Path
s=importlib.util.spec_from_file_location('replay',Path(__file__).with_name('run-prepared-replay.py'))
R=importlib.util.module_from_spec(s);s.loader.exec_module(R)
def run(base):
    models=R.check(base);data=json.loads((base/'dynamic-cases.json').read_text());cases=data['cases']
    R.require(len(cases)==14,'Need exact14 cases')
    for c in cases:
        R.require(c['id'].startswith('case') and c['id'][4:].isdigit() and c['model'] in models,'Unsafe case')
        for i,e in enumerate(c['inputs']):
            p=base/c['id']/('input%d.bin'%i)
            R.require(not p.is_symlink() and p.stat().st_size==e['bytes'] and R.sha(p)==e['sha256'],'Changed dynamic input')
    R.require(platform.machine()=='aarch64' and os.geteuid()==0 and Path('/dev/vipcore').is_char_device(),'Use physical Linux via sudo')
    R.require(not [p for p in glob.glob('/proc/[0-9]*/fd/*') if os.path.realpath(p)=='/dev/vipcore'],'NPU busy')
    R.require(shutil.disk_usage(base).free>2*1024**3,'Need 2GiB')
    work=Path(tempfile.mkdtemp(prefix='prepared-dynamic-',dir=base));rows=[];result='FAILED';error=None;previous=None
    outputs_by_model={k:set() for k in models};inputs_by_model={k:set() for k in models};deadline=time.monotonic()+600
    try:
        for c in cases:
            R.require(time.monotonic()<deadline,'Overall deadline');temperature=R.temperatures()
            incoming=work/(c['id']+'-input');incoming.mkdir()
            for i,entry in enumerate(c['inputs']):
                source=base/c['id']/('input%d.bin'%i)
                if c['model']=='encoder' and previous and 1<=i<=37:
                    source=work/(previous+'-output')/('output%d.bin'%i)
                target=incoming/('input%d.bin'%i);shutil.copyfile(source,target)
                R.require(R.sha(target)==entry['sha256'],'Actual recurrent state/input mismatch')
            print('DYNAMIC',c['id'],c['model'],c['origin'],flush=True)
            m=models[c['model']];out=work/(c['id']+'-output')
            with (work/(c['id']+'.log')).open('xb') as log:
                p=subprocess.run(['timeout','--signal=TERM','--kill-after=5s','60s',str(base/'tensor-check-linux-aarch64'),str(base/m['package']),'--run',str(m['command']['native_command_bytes']),str(incoming),str(out)],stdout=log,stderr=subprocess.STDOUT,timeout=66)
            R.require(p.returncode==0,'Native prepared replay failure exit='+str(p.returncode)+'; no retry/reset')
            hashes=[]
            for i,entry in enumerate(c['outputs']):
                path=out/('output%d.bin'%i)
                R.require(path.is_file() and path.stat().st_size==entry['bytes'] and R.sha(path)==entry['sha256'],'Dynamic golden mismatch '+c['id']+':'+str(i))
                hashes.append(R.sha(path))
            inputs_by_model[c['model']].add(tuple(e['sha256'] for e in c['inputs']))
            outputs_by_model[c['model']].add(tuple(hashes))
            rows.append(dict(id=c['id'],model=c['model'],origin=c['origin'],outputs=hashes,temperature_before=temperature))
            if c['model']=='encoder':previous=c['id']
            print(c['id'],'all outputs match native golden',flush=True)
        R.require(all(len(inputs_by_model[k])>=2 and len(outputs_by_model[k])>=2 for k in models),'No changing-input/output proof')
        result='A7TV_LINUX_DYNAMIC_GOLDENS_PASSED'
    except (ValueError,OSError,subprocess.SubprocessError) as e:error=str(e);print('STOP:',error,flush=True)
    finally:
        (work/'summary.json').write_text(json.dumps(dict(result=result,error=error,cases=rows,dynamic_input_verified=error is None,
            encoder_feedback_state_links=74 if error is None else None,file_based_diagnostic=True,openvela_executed=False),indent=2))
        archive=base/(work.name+'.tar.gz')
        with tarfile.open(archive,'x:gz') as tar:tar.add(work,arcname=work.name)
        if os.environ.get('SUDO_UID','').isdigit():os.chown(archive,int(os.environ['SUDO_UID']),int(os.environ['SUDO_GID']))
        print('REPORT:',archive,flush=True);print('RESULT:',result,flush=True)
    return 2 if error else 0
if __name__=='__main__':
    import sys
    R.require(sys.argv[1:]==['--npu-idle-confirmed'],'Explicit idle confirmation required')
    raise SystemExit(run(Path(__file__).resolve().parent))
