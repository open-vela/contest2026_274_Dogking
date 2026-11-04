#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Read-only latency report from explicit offline turns; no physical-onset claim."""
from pathlib import Path
import argparse,hashlib,json,re
p=argparse.ArgumentParser(description=__doc__);p.add_argument('serial',type=Path);a=p.parse_args()
raw=a.serial.read_bytes();text=raw.decode('utf-8',errors='replace')
turns=[]
for block in re.split(r'(?=offline-v190: turn=\d+/\d+)',text)[1:]:
    block=block.split('offline-v190: MIC OFF',1)[0]
    turn=re.search(r'offline-v190: turn=(\d+)/(\d+)',block)
    asr=re.search(r'mic-stopped-mono-us=(\d+) transcript-ready-mono-us=(\d+)',block)
    pulls=re.findall(r'tts-start-mono-us=(\d+) first-driver-pull-mono-us=(\d+) status=(-?\d+)',block)
    record=dict(turn=int(turn[1]),asr_text_present=bool(re.search(r'^\[asr\] \S',block,re.M)),
        physical_end_of_speech_known=False,physical_audio_onset_known=False)
    if asr and all(int(x)>0 for x in asr.groups()):
        stopped,ready=map(int,asr.groups())
        assert ready>=stopped,'non-monotonic ASR timestamps'
        record['capture_stop_to_transcript_ms']=(ready-stopped)/1000
        successful=[(int(start),int(pull)) for start,pull,status in pulls if int(status)==0 and int(pull)>=ready]
        if successful and record['asr_text_present']:
            start,pull=min(successful,key=lambda x:x[1])
            assert pull>=start>=ready
            record.update(capture_stop_to_driver_proxy_ms=(pull-stopped)/1000,
                transcript_to_first_phrase_start_ms=(start-ready)/1000,
                first_phrase_start_to_driver_ms=(pull-start)/1000)
    turns.append(record)
print(json.dumps(dict(serial_sha256=hashlib.sha256(raw).hexdigest(),turns=turns,
    goal_3_to_5_seconds='NOT_EVALUATED: energy endpoint/capture stop and driver pull are not physical EOS/onset',
    standalone_tts_first_sink_us=[int(x) for x in re.findall(r'first-sink-us=(\d+)',text)],
    full_voice_chain_accepted=False),ensure_ascii=False,indent=2))
