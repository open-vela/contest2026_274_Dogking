#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Seal local v181 evidence after full link; never publishes an SD image."""
import argparse
import hashlib
import json
import re
import shutil
from pathlib import Path

repo = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('build', type=Path)
p.add_argument('output', type=Path)
a = p.parse_args()
overlay = repo / 'board/a733-cubie-a7z/openvela-overlay'
native = (repo / '../../quickly-openvela/cmake_out/cubie-a7z_nsh_v181_voice_latency').resolve()

def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        while data := stream.read(1024 * 1024):
            h.update(data)
    return h.hexdigest()

staged = json.loads((a.build / 'staged.json').read_text())
for name, value in staged['files'].items():
    assert sha(overlay / name) == value, 'Changed since staging: ' + name
restored = json.loads((a.build / 'sdk-restored.json').read_text())
assert restored['result'] == 'SDK_RESTORED_BYTE_EXACT'
symbols = (a.build / 'symbols.txt').read_text()
for name in ('aipetllm_prefill_control', 'aipet::voice_play_stream(', 'aipet::routed_offline_stream_ask('):
    assert name in symbols, name
arm = json.loads((repo / 'out/voice-arm-v181-r2/summary.json').read_text())
assert len(arm) == 12
for name, info in arm.items():
    assert info['passed'] and info['max_static_frame'] <= 4096
    assert sha(overlay / name) == info['sha256']
asr = json.loads((repo / 'out/asr-stream-host-v181-r3/summary.json').read_text(encoding='utf8'))
assert asr['result'] == 'ONLINE_ASR_CPU_ORACLE_EXACT'
assert len(asr['cases']) == 3
assert all(x['same_all_logical_encoder_outputs'] and x['same_tokens_positions'] and
           x['chunks_before_finish'] > 0 for x in asr['cases'])
tts = json.loads((repo / 'out/tts-host-v181-r1/summary.json').read_text(encoding='utf8'))
assert len(tts['tts']['cases']) == 12 and all(x['exact_reference'] for x in tts['tts']['cases'])
real = {}
for model in ('minicpm', 'qwen'):
    path = repo / ('out/v181-' + model + '-batch-r1.log')
    text = path.read_text()
    comparisons = re.findall(r'LOGITS prompt=.*?maxabs=([0-9.e+-]+) rmse=([0-9.e+-]+)', text)
    assert len(comparisons) == 13 and all(float(x) == 0 and float(y) == 0 for x, y in comparisons)
    assert 'warm-cold=exact PASS' in text
    real[model] = {'full_logits_compared': 13, 'maxabs': 0, 'log_sha256': sha(path)}
bench_path = repo / 'out/v181-minicpm-bench-r1.log'
bench = re.findall(r'PREFILL_BENCH width=(\d) prompt=(\d+) first=([0-9.]+) logits=exact host-only', bench_path.read_text())
assert [int(x[0]) for x in bench] == [1, 4, 1, 4]
times = {w: [float(t) for width, _, t in bench if int(width) == w] for w in (1, 4)}
speed = 1 - sum(times[4]) / sum(times[1])
assert speed > 0
logs = {
    'out/v181-voice-stream-build-r6.log': 'PASS: streaming resampler',
    'out/v181-i2s-stream-r1.log': 'PASS: actual I2S stream TX function',
    'out/v181-i2s-live-r2.log': 'PASS: actual RX loop',
    'out/v181-routes-r2.log': 'bounded ordered routes/config/template tests passed',
    'out/v181-llm-app-regression-r1.log': 'PASS: atomic config, bounded RAM dialogue',
    'out/v181-audio-app-regression-r2.log': 'speech output contracts passed',
    'out/v181-mcu-regression-r1.log': 'NOT board acceptance',
    'out/v181-asr-real-stream-r3.log': 'PASS: early encoder execution',
    'out/v181-tts-real-r1.log': 'PASS: host semantic oracle only',
    'out/v181-minicpm-batch-r1.log': 'warm-cold=exact PASS',
    'out/v181-qwen-batch-r1.log': 'warm-cold=exact PASS',
    'out/v181-minicpm-bench-r1.log': 'PREFILL_BENCH',
}
for name, success in logs.items():
    text = (repo / name).read_text(encoding='utf8', errors='strict')
    assert success in text and 'ERROR: AddressSanitizer' not in text, name
for name in ('nuttx.bin', 'nuttx', '.config'):
    assert (native / name).is_file(), name
a.output.mkdir()
artifacts = {}
for name, dest in (('nuttx.bin', 'Image'), ('nuttx', 'nuttx.elf'), ('.config', 'build.config')):
    out = a.output / dest
    shutil.copyfile(native / name, out)
    artifacts[dest] = {'bytes': out.stat().st_size, 'sha256': sha(out)}
evidence = repo / 'docs/evidence/voice-latency-v181'
evidence.mkdir()
for name in logs:
    shutil.copyfile(repo / name, evidence / Path(name).name)
record = {'result': 'V181_LATENCY_SOFTWARE_VERIFIED_NOT_FULL_TARGET_SPEECH',
          'staged_sources': staged['files'], 'sdk_restored': restored,
          'arm64_units': arm, 'asr_cpu_oracle': asr, 'tts_cpu_oracle': tts,
          'real_llm': real, 'host_prefill': {'times': times, 'fraction_faster': speed,
                                          'board_proof': False},
          'logs': {name: sha(repo / name) for name in logs},
          'build_log_sha256': sha(a.build / 'build.log'),
          'artifacts': artifacts, 'new_sd_image': False,
          'board_executed': False, 'full_offline_chain_complete': False,
          'target_general_asr_provider': False, 'target_matcha_acoustic_executor': False,
          'first_audible_3_to_4_seconds_accepted': False,
          'asr_stream_retained_in_final_elf': 'aipet::voice_asr_stream_open(' in symbols}
(a.output / 'manifest.json').write_text(json.dumps(record, ensure_ascii=False, indent=2), encoding='utf8')
(evidence / 'summary.json').write_text(json.dumps(record, ensure_ascii=False, indent=2), encoding='utf8')
print(json.dumps({'result': record['result'], 'artifacts': artifacts, 'host_prefill': record['host_prefill']}, indent=2))
