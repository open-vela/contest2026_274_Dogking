#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Real CPU model oracle for production online ASR core; no NPU/board claims."""
import argparse,hashlib,importlib.util,json,struct,time,wave
from pathlib import Path
spec=importlib.util.spec_from_file_location('oracle',Path(__file__).with_name('test-host-speech-v179.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
np=m.np
p=argparse.ArgumentParser(description=__doc__);p.add_argument('output',type=Path)
p.add_argument('--build',type=Path,default=m.REPO/'out/voice-stream-v181-r1')
p.add_argument('--recorded-wav',type=Path,help='Optional private <=8s S16 mono/stereo 16/48k CPU diagnostic; no target inference')
a=p.parse_args()
a.output.mkdir()
lock=m.load_module('locked',Path(__file__).with_name('fetch-asr-source.py'))
source=m.REFS/'zipformer-asr-upstream-e238275';lock.verify(source)
models={r:m.session(source/next(n for n in lock.FILES if n.startswith(r))) for r in ('encoder','decoder','joiner')}
enc,dec,join=(models[r] for r in ('encoder','decoder','joiner'))
gold=json.loads((m.REPO/'docs/evidence/local-voice-v179/host-speech-summary.json').read_text(encoding='utf8'))['asr']['cases']
results=[]
if a.recorded_wav:gold.append({'case':'private-recording-cpu-diagnostic','recorded':True})
for expected in gold:
    recorded=expected.get('recorded',False)
    wav=a.recorded_wav if recorded else m.REFS/'cubie-a7a-voice-assistant-main/samples'/(expected['case']+'.wav')
    if not recorded:assert m.sha(wav)==expected['source_sha256']
    with wave.open(str(wav),'rb') as f:
        assert f.getsampwidth()==2 and f.getnchannels() in (1,2) and f.getframerate() in (16000,48000)
        rate=f.getframerate();channels=f.getnchannels();samples=f.getnframes()
        assert 0<samples<=rate*8
        raw=f.readframes(samples)
    if recorded:
        values=np.frombuffer(raw,dtype='<i2').reshape(-1,channels)[:,0].astype(np.float64)
        if rate==48000:
            # CPU diagnostic reproduction of the locked 63-tap FIR formula;
            # NOT proof that the board captured or resampled these samples.
            assert len(values)%3==0
            k=np.arange(63,dtype=np.float64)-31;cutoff=6500.0/48000.0
            taps=2*cutoff*np.sinc(2*cutoff*k)*(0.54-0.46*np.cos(2*np.pi*np.arange(63)/62))
            values=np.convolve(values,taps/taps.sum(),mode='same')[::3]
            values=np.where(values>=0,np.floor(values+0.5),np.ceil(values-0.5))
        raw=np.clip(values,-32768,32767).astype('<i2').tobytes();samples=len(raw)//2
    state={}
    for v in enc.get_inputs()[1:]:
        shape=[1 if d=='N' else d for d in v.shape]
        state[v.name]=np.zeros(shape,dtype=np.int64 if v.type=='tensor(int64)' else np.float32)
    b=m.Bridge(a.build/'asr-stream-host');chunks=0;sent=0;first=None;early=0
    traces=[];started=time.monotonic()
    def service():
        global chunks,first,early
        while True:
            op=b.read(1)
            if op==b'X':
                payload=b.read(103*80*4)
                if not recorded:assert hashlib.sha256(payload).hexdigest()==expected['logical_onnx_outputs'][chunks]['inputs']['x']
                feeds={'x':np.frombuffer(payload,dtype='<f4').reshape(1,103,80),**state}
                out=enc.run(None,feeds)
                assert out[0].shape==(1,24,512) and all(np.isfinite(v).all() for v in out)
                actual={v.name:hashlib.sha256(x.tobytes()).hexdigest() for v,x in zip(enc.get_outputs(),out)}
                if not recorded:assert actual==expected['logical_onnx_outputs'][chunks]['outputs']
                for v,x in zip(enc.get_inputs()[1:],out[1:]):state[v.name]=x
                if first is None:first=sent//2
                early+=sent<len(raw);traces.append(actual);chunks+=1
                b.write(bytes(4)+out[0].astype('<f4').tobytes())
            elif op==b'D':
                y=np.frombuffer(b.read(16),dtype='<i8').reshape(1,2)
                out=dec.run(None,{'y':y})[0];b.write(bytes(4)+out.astype('<f4').tobytes())
            elif op==b'J':
                pair=np.frombuffer(b.read(4096),dtype='<f4').reshape(2,512)
                out=join.run(None,{'encoder_out':pair[:1],'decoder_out':pair[1:]})[0]
                b.write(bytes(4)+out.astype('<f4').tobytes())
            elif op==b'K':return struct.unpack('<III',b.read(12))
            elif op==b'R':
                n=struct.unpack('<I',b.read(4))[0]
                return np.frombuffer(b.read(n*8),dtype='<u4').reshape(n,2)
            elif op==b'E':raise ValueError('ASR core failed '+str(struct.unpack('<i',b.read(4))[0]))
            else:raise ValueError('Unknown '+repr(op))
    assert b.read(1)==b'D';y=np.frombuffer(b.read(16),dtype='<i8').reshape(1,2)
    assert not y.any();b.write(bytes(4)+dec.run(None,{'y':y})[0].astype('<f4').tobytes())
    while sent<len(raw):
        n=min(3200,len(raw)-sent);part=raw[sent:sent+n];sent+=n
        b.write(struct.pack('<I',n)+part);accepted=service();assert accepted[0]==sent//2
    before=chunks;b.write(bytes(4));actual=service();b.close()
    if not recorded:
        assert actual[:,0].tolist()==expected['token_ids'] and actual[:,1].tolist()==expected['frame_positions']
        assert chunks==len(expected['logical_onnx_outputs']) and early>0
    tokpath=m.REFS/'awnpu_model_zoo-v1.1.0-20260910-675a8dcb/examples/zipformer/model/tokens.txt'
    assert m.sha(tokpath)=='a8e0e4ec53810e433789b54a5c0134a7eaa2ffca595a6334d54c00da858841d3'
    tokens={int(line.rsplit(' ',1)[1]):line.rsplit(' ',1)[0] for line in tokpath.read_text(encoding='utf8').splitlines()}
    text=''.join(tokens[int(t)] for t in actual[:,0]).replace('▁',' ').strip()
    if not recorded:assert text==expected['text']
    result={'case':expected['case'],'text':text,'source_sha256':m.sha(wav),'chunks_before_finish':before,
            'chunks_total':chunks,'first_encoder_at_input_seconds':first/16000,
            'same_all_logical_encoder_outputs':not recorded,'same_tokens_positions':not recorded,
            'private_file_diagnostic':recorded,'microphone_executed':False,
            'host_wall_seconds':time.monotonic()-started,'host_only':True,'npu_executed':False}
    results.append(result);print(json.dumps(result,ensure_ascii=False),flush=True)
record={'result':'ONLINE_ASR_CPU_ORACLE_EXACT_WITH_PRIVATE_FILE_DIAGNOSTIC' if a.recorded_wav else 'ONLINE_ASR_CPU_ORACLE_EXACT','cases':results,'board_executed':False,
        'native_nbg_layout_qualified':False,'full_offline_chain':False}
(a.output/'summary.json').write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf8')
print('PASS: early encoder execution, exact fbank/state/decoder semantics; not target NPU acceptance')
