#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Fresh A7TV neural KWS graphs + shared application C search; file features.

No native NBG runner, frozen output tensors, live mic, ASR, TTS or cloud calls.
"""
import glob,importlib.util,json,os,platform,shutil,struct,subprocess,tarfile,tempfile,time
from pathlib import Path
def module(name,file):
    s=importlib.util.spec_from_file_location(name,Path(__file__).with_name(file));m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
R=module('prepared_replay','run-prepared-replay.py');K=module('prepared_search','run-kws-search.py')
class Runner:
    def __init__(self,base,work,models,oracles):
        self.base,self.work,self.models,self.oracles=base,work,models,oracles;self.calls=0;self.deadline=time.monotonic()+900
    def invoke(self,tag,name,inputs):
        R.require(self.calls<6000 and time.monotonic()<self.deadline,'Call/time budget')
        R.temperatures();m=self.models[name]
        R.require(len(inputs)==len(m['inputs']) and all(len(d)==e['bytes'] for d,e in zip(inputs,m['inputs'])),'Native input lengths')
        key=name+':'+','.join(K.sha(d) for d in inputs)
        R.require(key in self.oracles,'Missing known native hash oracle; stop before hardware')
        self.calls+=1;prefix='call%05d'%self.calls;folder=self.work/(prefix+'-input');folder.mkdir()
        for i,d in enumerate(inputs):(folder/('input%d.bin'%i)).write_bytes(d)
        output=self.work/(prefix+'-output')
        with (self.work/(prefix+'.log')).open('xb') as log:
            p=subprocess.run(['timeout','--signal=TERM','--kill-after=5s','60s',str(self.base/'tensor-check-linux-aarch64'),str(self.base/m['package']),'--run',str(m['command']['native_command_bytes']),str(folder),str(output)],stdout=log,stderr=subprocess.STDOUT,timeout=66)
        R.require(p.returncode==0,'Prepared execution failed; stop, no reset/retry')
        values=[]
        for i,e in enumerate(m['outputs']):
            path=output/('output%d.bin'%i);R.require(path.is_file() and path.stat().st_size==e['bytes'],'Output missing/length')
            raw=path.read_bytes();K.SK.floats(raw)
            R.require(K.sha(raw)==self.oracles[key][i],'Native oracle mismatch '+name+':'+str(i));values.append(raw)
        with (self.work/'tensor-trace.jsonl').open('a') as stream:stream.write(json.dumps(dict(call=self.calls,tag=tag,model=name,inputs=[K.sha(d) for d in inputs],outputs=[K.sha(d) for d in values]))+'\n')
        return values
def run(base):
    models=R.check(base);index=json.loads((base/'kws-cases.json').read_text());contract=json.loads((base/'kws-contract.json').read_text())
    R.require(platform.machine()=='aarch64' and os.geteuid()==0 and Path('/dev/vipcore').is_char_device(),'Physical Linux via sudo only')
    R.require(not [p for p in glob.glob('/proc/[0-9]*/fd/*') if os.path.realpath(p)=='/dev/vipcore'],'NPU busy')
    R.require(shutil.disk_usage(base).free>2*1024**3,'Need2GiB')
    vocabulary={line.rsplit(maxsplit=1)[0]:int(line.rsplit(maxsplit=1)[1]) for line in (base/'tokens.txt').read_text().splitlines()}
    words=K.keywords((base/'keywords.txt').read_text(),vocabulary)
    work=Path(tempfile.mkdtemp(prefix='prepared-kws-',dir=base));runner=Runner(base,work,models,index['oracles']);cases=[];error=None;result='FAILED'
    oracle=K.Oracle(base,work,dict(logits=[]),runner) # no frozen outputs, native cache only
    try:
        for record in index['cases']:
            features=(base/record['kit_features']).read_bytes();R.require(K.sha(features)==record['feature_sha256'],'Features changed')
            states=[bytes(d['bytes']) for d in contract['encoder']['input']];search=K.Search(base/'libaipet-kws-aarch64.so',words);hits=[];frame=0
            for chunk in range(record['chunks']):
                offset=chunk*16;states[0]=features[offset*320:(offset+29)*320];states[38]=struct.pack('<f',offset)
                out=runner.invoke(record['tag']+'-c%d'%chunk,'encoder',states);states=[b'']+out[1:]
                for f in range(4):
                    enc=out[0][f*1280:(f+1)*1280];contexts=search.contexts()
                    logits=[oracle.query(enc,ctx,record['tag']+'-f%d-r%d'%(frame,i))[0] for i,ctx in enumerate(contexts)]
                    hit=search.advance(logits)
                    if hit:hits.append(hit)
                    frame+=1
                print('KWS',record['tag'],'chunk=%d/%d'%(chunk+1,record['chunks']),'hits='+repr([h['phrase'] for h in hits]),'prepared_calls='+str(runner.calls),flush=True)
            names=[h['phrase'] for h in hits];ok=record['expected'] in names if record['expected'] else not names
            cases.append(dict(scenario=record['tag'],expected=record['expected'],hits=hits,exact_phrase_ok=ok))
            R.require(ok,'Keyword regression failed '+record['tag'])
        result='A7TV_LINUX_KWS_SIX_FIXTURES_PASSED'
    except (ValueError,OSError,subprocess.SubprocessError) as e:error=str(e);print('STOP:',error,flush=True)
    finally:
        (work/'summary.json').write_text(json.dumps(dict(result=result,error=error,cases=cases,calls=runner.calls,
            cached_native_queries=oracle.reused,fresh_joiner_queries=oracle.fresh,precomputed_fbank=True,
            neural_backend='A7TV/Linux VIP2',search_backend='shared aipet C core',frozen_outputs_used=False,
            live_microphone=False,asr=False,openvela_executed=False,end_to_end_voice_assistant=False),ensure_ascii=False,indent=2))
        archive=base/(work.name+'.tar.gz')
        with tarfile.open(archive,'x:gz') as t:t.add(work,arcname=work.name)
        if os.environ.get('SUDO_UID','').isdigit():os.chown(archive,int(os.environ['SUDO_UID']),int(os.environ['SUDO_GID']))
        print('REPORT:',archive,flush=True);print('RESULT:',result,flush=True)
    return 2 if error else 0
if __name__=='__main__':
    import sys
    R.require(sys.argv[1:]==['--npu-idle-confirmed'],'Explicit idle confirmation required');raise SystemExit(run(Path(__file__).resolve().parent))
