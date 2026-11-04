#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Private, hash-locked ASR board test WAVs; no model or personal recording.

Creates a NEW ignored output folder/zip only. Does not write SD/UFS or use
network. Copying a WAV to the board is explicit file-input testing, not MIC
acceptance. Existing private/model-license policy remains in force.
"""
import argparse, hashlib, json, shutil, zipfile
from pathlib import Path
REPO=Path(__file__).resolve().parents[2]
LOCK={
 'cmd_open_light_16k.wav':('be2add3f8e1cdf8c328cb6464d294713832120c3e2ebbce092483f95423a4077','帮我打开灯'),
 'cmd_time_16k.wav':('c9d88dcce6b6b50a5ecce836a2b88736850cebad93c5e75e39f90a0c81b404d2','现在几点了'),
 'neg_silence_3s_16k.wav':('d303811b8c84619667cd0501342f84ec6cbe69f7aa3856dcf52fabda374c92b8',''),
}
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('new_output',type=Path)
    args=parser.parse_args();out=args.new_output.resolve()
    assert out.is_relative_to(REPO/'out') and not out.exists()
    source=REPO.parent/'references/cubie-a7a-voice-assistant-main/samples'
    for name,(sha,_) in LOCK.items():assert digest(source/name)==sha
    out.mkdir();checks=out/'checks';checks.mkdir();entries=[]
    for name,(sha,text) in LOCK.items():
        shutil.copyfile(source/name,checks/name)
        assert digest(checks/name)==sha
        entries.append(dict(file='checks/'+name,sha256=sha,expected_text=text,
                            expected_result=0 if text else -61))
    record=dict(result='PRIVATE_TEST_INPUTS_NOT_BOARD_ACCEPTANCE',files=entries,
                models_included=False,personal_audio_included=False,hardware_executed=False)
    (out/'manifest.json').write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    (out/'README.txt').write_text(
        'PRIVATE LOCAL TESTS ONLY. No models/credentials/personal recordings.\n'
        'Copy ONLY checks/ to SD data partition: npu/general-asr/checks/\n'
        'This does not enable recording, perform inference or modify existing models.\n'
        'aipet audio general-asr /data/npu/general-asr\n'
        'aipet audio general-wav /data/npu/general-asr/checks/cmd_open_light_16k.wav\n'
        'aipet audio general-wav /data/npu/general-asr/checks/cmd_time_16k.wav\n'
        'aipet audio general-wav /data/npu/general-asr/checks/neg_silence_3s_16k.wav\n'
        'Silence MUST return -61 with no reply/actions, not a canned success.\n'
        'File-input success does NOT prove live microphone or full voice chain.\n',encoding='utf8')
    with zipfile.ZipFile(out.with_suffix('.zip'),'x',compression=zipfile.ZIP_DEFLATED) as archive:
        for file in sorted(out.rglob('*')):
            if file.is_file():archive.write(file,file.relative_to(out).as_posix())
    print(json.dumps(dict(result=record['result'],zip=str(out.with_suffix('.zip')),
                         sha256=digest(out.with_suffix('.zip')),files=len(entries))))
if __name__=='__main__':main()
