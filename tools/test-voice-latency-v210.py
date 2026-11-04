#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import json,subprocess,sys,tempfile
r=Path(__file__).resolve().parents[1]
serial="""unrelated standalone TTS first-sink-us=2803807
offline-v190: turn=1/2
[asr] 现在几点了
voice-stage: mic-stopped-mono-us=1000000 transcript-ready-mono-us=1500000
voice-stage: tts-start-mono-us=3800000 first-driver-pull-mono-us=4200000 status=0
voice-stage: tts-start-mono-us=5000000 first-driver-pull-mono-us=5300000 status=0
offline-v190: turn=2/2
voice-stage: mic-stopped-mono-us=6000000 transcript-ready-mono-us=0
voice-stage: tts-start-mono-us=7000000 first-driver-pull-mono-us=7200000 status=-5
offline-v190: MIC OFF
"""
with tempfile.TemporaryDirectory(prefix='voice210-latency-') as directory:
    f=Path(directory)/'serial.txt';f.write_text(serial)
    obj=json.loads(subprocess.check_output([sys.executable,str(r/'tools/analyze-voice-latency-v210.py'),str(f)]))
    assert len(obj['turns'])==2 and obj['turns'][0]['capture_stop_to_driver_proxy_ms']==3200
    assert obj['turns'][0]['first_phrase_start_to_driver_ms']==400
    assert 'capture_stop_to_driver_proxy_ms' not in obj['turns'][1]
    assert not obj['full_voice_chain_accepted'] and obj['goal_3_to_5_seconds'].startswith('NOT_EVALUATED')
print('PASS latency parser: per-turn monotonic software proxy, failed ASR/driver excluded, no physical latency claim')
