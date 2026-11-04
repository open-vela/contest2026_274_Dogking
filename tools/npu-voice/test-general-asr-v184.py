#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Host native-capture replay of the real target ASR adapter; NO NPU calls."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import wave

REPO = Path(__file__).resolve().parents[2]


def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('new_output',type=Path)
    a=p.parse_args();out=a.new_output.resolve()
    assert out.is_relative_to(REPO/'out')
    out.mkdir()
    native=REPO/'out/asr-native-v183-r1/unpacked/asr-real-r2'
    calls=[json.loads(s) for s in (native/'calls.jsonl').read_text().splitlines()]
    summary=json.loads((native/'summary.json').read_text())
    assert summary['result']=='NATIVE_GENERAL_ASR_TEXT_MATCH' and len(calls)==216
    with (out/'calls.txt').open('x') as f:
        for call in calls:
            folder=native/call['label']
            for side in ('input','output'):
                for i,expected in enumerate(call[side+'_sha256']):
                    assert digest(folder/side/('%s%d.bin'%(side,i)))==expected
            assert not any(c.isspace() for c in str(folder))
            f.write('%s %s\n'%(call['role'],folder))
    kit=REPO/'out/a733-linux-general-asr-v183-r1'
    assert digest(kit/'tokens.txt')=='a8e0e4ec53810e433789b54a5c0134a7eaa2ffca595a6334d54c00da858841d3'
    model=out/'model';model.mkdir();shutil.copyfile(kit/'tokens.txt',model/'tokens.txt')
    for case in summary['cases']:
        name=case['case']
        wav=kit/'fixtures'/name/'source.wav'
        with wave.open(str(wav),'rb') as w:
            assert w.getnchannels()==1 and w.getsampwidth()==2 and w.getframerate()==16000
            (out/(name+'.s16')).write_bytes(w.readframes(w.getnframes()))
    pet=REPO/'board/a733-cubie-a7z/openvela-overlay/apps/system/aipet'
    knf=pet/'third_party/kaldi-native-fbank/csrc'
    flags=['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-Wall','-Wextra','-Werror']
    subprocess.run(['gcc','-std=c11',*flags,'-I'+str(pet),'-c',str(pet/'voice_asr_decode.c'),'-o',str(out/'decode.o')],check=True)
    cpp=['g++','-std=c++17',*flags,'-Wno-sign-compare','-Wno-unused-parameter',
         '-I'+str(pet),'-I'+str(pet.parent.parent/'include'),'-I'+str(knf/'include'),
         str(REPO/'tools/test-general-asr-native-v184.cxx'),str(pet/'voice_general_asr.cxx'),
         str(pet/'voice_asr_stream.cxx'),str(pet/'voice_fbank.cxx'),
         *map(str,sorted((knf/'src').glob('*.cc'))),str(out/'decode.o'),'-lm','-o',str(out/'test')]
    with (out/'compile.log').open('xb') as log:
        subprocess.run(cpp,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (out/'run.log').open('xb') as log:
        subprocess.run([str(out/'test'),str(model.relative_to(REPO)),str(out)],cwd=REPO,
                       stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
    print((out/'run.log').read_text())
    (out/'summary.json').write_text(json.dumps(dict(result='HOST_CAPTURE_REPLAY_PASS',
        hardware_executed=False,openvela_provider_qualified=False,full_voice_chain=False,
        source_sha256={p.name:digest(p) for p in (pet/'voice_general_asr.cxx',pet/'voice_general_contract.hxx')},
        compile_command=cpp),indent=2))


if __name__=='__main__':main()
