#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1];p=r/'board/a733-cubie-a7z/openvela-overlay/apps/system/aipet'
knf=p/'third_party/kaldi-native-fbank/csrc'
with tempfile.TemporaryDirectory(prefix='tail211-') as directory:
    d=Path(directory);old=d/'old.cxx'
    name=str((p/'voice_asr_stream.cxx').relative_to(r))
    old.write_bytes(subprocess.check_output(['git','show','a733-v210-voice-source-20261030:'+name],cwd=r))
    flags=['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
    subprocess.run(['gcc','-std=c11',*flags,'-I'+str(p),'-c',str(p/'voice_asr_decode.c'),'-o',str(d/'decode.o')],check=True)
    names=dict(VoiceAsrStream='OldVoiceAsrStream',voice_asr_stream_open='old_open',
      voice_asr_stream_accept='old_accept',voice_asr_stream_finish='old_finish',
      voice_asr_stream_close='old_close',voice_asr_stream_stats='old_stats',voice_asr_stream_result='old_result')
    cpp=['g++','-std=c++17',*flags,'-I'+str(p),'-I'+str(knf/'include')]
    subprocess.run([*cpp,*['-D'+k+'='+v for k,v in names.items()],'-c',str(old),'-o',str(d/'old.o')],check=True)
    subprocess.run([*cpp,str(r/'tools/test-asr-tail-v211.cxx'),str(p/'voice_asr_stream.cxx'),str(p/'voice_fbank.cxx'),
      *map(str,sorted((knf/'src').glob('*.cc'))),str(d/'old.o'),str(d/'decode.o'),'-lm','-o',str(d/'test')],check=True)
    subprocess.run([str(d/'test')],check=True,timeout=120)
