#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Promote the EXACT user-accepted image; never rebuild or rewrite it."""
from pathlib import Path
import argparse, hashlib, json, re, shutil

r = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('serial', type=Path)
a = p.parse_args()

def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda: f.read(8 * 1024 * 1024), b''):
            h.update(b)
    return h.hexdigest()

image = r.parent / 'openvela-a733-cubie-a7z-sd-v1.1-v219-candidate.img'
digest = 'fe7a28ce9996e3344a1866610c7cdb9137a1088965b0207161df6ffab4c3359f'
assert image.stat().st_size == 2147483648 and sha(image) == digest
raw = a.serial.read_text(encoding='utf-8-sig')
# Serial export wraps inside numbers: reconstruct diagnostic records only.
joined = re.sub(r'(?<=\d)\r?\n(?=\d)', '', raw)
llm = [dict(prompt=int(n), generated=int(g), first_s=float(t), rest_s=float(d))
       for n, g, t, d in re.findall(
           r'aipet-local: prompt=(\d+) generated=(\d+) first=([\d.]+)s rest=([\d.]+)s', joined)]
assert len(llm) == 7 and all(x['generated'] > 0 for x in llm)
tails = [int(x) for x in re.findall(r'tail-us=(\d+) status=0', joined)]
assert len(tails) == 8 and 'Connected; WPA2 and DHCP passed.' in raw
assert raw.count('product-v1: stopped=-125;') == 2
assert 'local-tts: result=0' in raw and raw.count('product-vision-v1: token=') == 2
private = r / 'out/v219-board-accepted-20261101'
private.mkdir(exist_ok=False)
shutil.copyfile(a.serial, private / 'serial-original.txt')
receipt = dict(
    result='V11_V219_USER_BOARD_ACCEPTED_LATEST_STABLE', date='2026-11-01',
    tag='a733-v1.1-v219-stable-20261101', image=image.name,
    bytes=image.stat().st_size, sha256=digest,
    firmware_commit='f6531dbc0db75590ef76904afe4c786ec4bd8503',
    source_baseline_commit='c7becd8eafdb21f7f54af407fb7eb7019316f26b',
    source_serial_sha256=sha(a.serial), raw_serial_published=False,
    user_reported_all_tested_functions_normal=True,
    observed=dict(wifi_WPA2_DHCP=True, local_ASR_LLM_TTS=True,
                  multi_turn=True, camera_YOLO_spoken_result=True,
                  cooperative_stop_restart=True, llm=llm, asr_tail_us=tails),
    image_rebuilt=False, image_modified=False,
    cloud_API_new_acceptance=False, physical_MCU_motion_new_acceptance=False,
    physical_audio_onset_instrumented=False,
    latency_3_to_5_s_accepted=False,
    note='Promotion uses user confirmation and attached serial evidence; optional untested cloud accounts are not implied accepted.')
dest = r / 'docs/evidence/product-v11-v219/board-accepted-20261101.json'
assert not dest.exists()
dest.write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
pointer = {k: receipt[k] for k in ('result', 'date', 'tag', 'image', 'bytes', 'sha256', 'firmware_commit')}
pointer['record'] = 'evidence/product-v11-v219/board-accepted-20261101.json'
(r / 'docs/STABLE_RELEASE.json').write_text(json.dumps(pointer, indent=2) + '\n', encoding='utf-8')
print(json.dumps(pointer, indent=2))
