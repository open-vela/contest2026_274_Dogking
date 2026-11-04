#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Offline consistency audit of ONE reported live run; never visual/HW proof."""
from pathlib import Path
import argparse, hashlib, json, re

ACCEPT = re.compile(r'live: accepted seconds=(\d+) detect-every=(\d+) ')
FRAME = re.compile(r'live frame=(\d+) input=([0-9a-f]{8}) boxes=(\d+) inferred=([01]) capture=(\d+) prepare=(\d+) npu=(\d+) display=(\d+) us')
NPU = re.compile(r'live npu seq=(\d+) outputs=([0-9a-f]{8}(?:/[0-9a-f]{8}){5}) fallback=0')
DET = re.compile(r'live det seq=(\d+) class=(\d+) score=(\d+) box=(\d+),(\d+),(\d+),(\d+)')
STATUS = re.compile(r'camera-live-v204: active=([01]) stop=([01]) stage=(\S+) error=(-?\d+) cleanup=(-?\d+) frames=(\d+) inference=(\d+) elapsed-ms=(\d+) fps=(\d+\.\d{3}) input-crc32=([0-9a-f]{8})')

def audit(text, run_index=-1):
    runs = []
    for line in text.splitlines():
        match = ACCEPT.search(line)
        if match:
            runs.append(dict(seconds=int(match[1]), every=int(match[2]), lines=[]))
        if runs:
            runs[-1]['lines'].append(line)
    if not runs:
        raise ValueError('Missing accepted line; full run log required')
    run = runs[run_index]
    if not 1 <= run['seconds'] <= 3600 or not 0 <= run['every'] <= 60:
        raise ValueError('Invalid declared run bounds')
    frames, npu, dets, terminals = {}, {}, {}, []
    for line in run['lines']:
        match = FRAME.search(line)
        if match:
            seq = int(match[1]); inferred = int(match[4]); boxes = int(match[3])
            if not 1 <= seq <= 32768 or seq in frames or (frames and seq <= max(frames)):
                raise ValueError('Duplicate/out-of-order frame')
            expected = bool(run['every'] and (seq-1) % run['every'] == 0)
            if bool(inferred) != expected or boxes > 7 or (not inferred and boxes):
                raise ValueError('Wrong inference cadence/preview boxes')
            frames[seq] = dict(crc=match[2], boxes=boxes, inferred=inferred,
                               phase_us=list(map(int,match.group(5,6,7,8))))
        match = NPU.search(line)
        if match:
            seq = int(match[1])
            if seq not in frames or seq != max(frames) or not frames[seq]['inferred'] or seq in npu:
                raise ValueError('NPU result not bound to a unique inferred frame')
            npu[seq] = match[2].split('/'); dets[seq] = []
        match = DET.search(line)
        if match:
            seq, cls, score, x1, y1, x2, y2 = map(int,match.groups())
            if seq not in npu or seq != max(frames) or not (0 <= cls <= 65535 and 0 <= score <= 1000 and 0 <= x1 < x2 <= 640 and 0 <= y1 < y2 <= 640):
                raise ValueError('Unbound/invalid reported box')
            dets[seq].append([cls,score,x1,y1,x2,y2])
        match = STATUS.search(line)
        if match and match[1] == '0':
            terminals.append(match.groups())
    if not terminals:
        raise ValueError('No inactive final summary; accepted/stop requested is not completion')
    final = terminals[-1]
    active, stop, stage, error, cleanup, count, inferred, elapsed, fps, last_crc = final
    count, inferred, elapsed = int(count), int(inferred), int(elapsed)
    if stage != 'complete' or int(error) or int(cleanup) or not 0 < count <= 32768 or elapsed <= 0:
        raise ValueError('Run not cleanly completed, or no completed frame')
    expected = 1+(count-1)//run['every'] if run['every'] else 0
    if inferred != expected or len(npu) != inferred or any(seq > count for seq in frames):
        raise ValueError('Incomplete log or inconsistent completed inference count')
    if not frames or min(frames) != 1:
        raise ValueError('Missing first preview/frame record')
    for seq, frame in frames.items():
        if frame['inferred'] and (seq not in npu or len(dets[seq]) != frame['boxes']):
            raise ValueError('Missing NPU output/detection lines')
    if count in frames and frames[count]['crc'] != last_crc:
        raise ValueError('Final input CRC does not match logged final frame')
    expected_fps_milli = count*1000000//elapsed
    if abs(round(float(fps)*1000)-expected_fps_milli) > 2:
        raise ValueError('FPS inconsistent with frame count/wall time')
    return dict(result='REPORTED_LIVE_RUN_LOG_CONSISTENT_NOT_HARDWARE_ACCEPTANCE',
        requested_seconds=run['seconds'],detect_every=run['every'],completed_frames=count,
        completed_inferences=inferred,elapsed_ms=elapsed,reported_fps=fps,stop_requested=bool(int(stop)),
        logged_frames=len(frames),distinct_logged_input_crc=len({x['crc'] for x in frames.values()}),
        detections=sum(map(len,dets.values())),zero_detection_inferences=sum(not x for x in dets.values()),
        last_input_crc=last_crc,binary_identity_verified=False,visual_accuracy_verified=False,
        hardware_verified=False,note='Only internal consistency of supplied text; preview-only frames are sparsely logged. Preserve original log and inspect real display/scene/temperature separately.')

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('log',type=Path);p.add_argument('new_json',type=Path)
    p.add_argument('--run-index',type=int,default=-1)
    a = p.parse_args()
    if a.log.stat().st_size > 64*1024*1024:
        raise ValueError('Log exceeds 64 MiB bound')
    data = a.log.read_bytes();result = audit(data.decode('utf-8','replace'),a.run_index)
    result['source_sha256'] = hashlib.sha256(data).hexdigest()
    with a.new_json.open('x',encoding='utf-8') as f:
        json.dump(result,f,indent=2);f.write('\n')
    print(result['result'])

if __name__ == '__main__':
    main()
