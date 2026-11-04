#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Bounded Linux general-ASR native evidence, NOT an RTOS provider.

36/36 recurrent encoder, 512-wide decoder/joiner, 6254 classes. Uses the
vendor runtime through the unchanged native runner and keeps EVERY native
input/output. No numpy/ONNX, KWS substitutions, playback, reset or retries.
The runner-per-call wall time is diagnostic, not resident-engine performance.
"""
import argparse
import array
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import stat
import struct
import subprocess
import sys
import time


def require(ok, why):
    if not ok:
        raise ValueError(why)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read(path, size=None):
    require(path.is_file() and not path.is_symlink(), 'not a regular file: ' + str(path))
    n = path.stat().st_size
    require(0 < n <= 128 * 1024 * 1024 and (size is None or n == size), 'bad file size: ' + str(path))
    return path.read_bytes()


def write(path, data):
    with path.open('xb') as f:
        f.write(data)


def record(path, data):
    write(path, (json.dumps(data, ensure_ascii=False, indent=2) + '\n').encode())


def floats(data):
    require(len(data) % 4 == 0 and sys.byteorder == 'little', 'expected little endian F32')
    a = array.array('f')
    a.frombytes(data)
    require(a.itemsize == 4 and all(math.isfinite(x) for x in a), 'nonfinite native float')
    return a


def metadata(log):
    m = re.findall(r'^inputs=(\d+) outputs=(\d+)$', log, re.M)
    require(len(m) == 1, 'missing native tensor counts')
    result = {}
    for kind, n in zip(('input', 'output'), map(int, m[0])):
        require(1 <= n <= 64, 'native tensor count bound')
        result[kind] = []
        for i in range(n):
            rows = re.findall(r'^' + kind + r'\[' + str(i) +
                              r'\] name=.+ padded-bytes=(\d+) format=(\d+) quant=(\d+) dims=([0-9x]+)$', log, re.M)
            require(len(rows) == 1, 'missing/duplicate native tensor ' + kind + str(i))
            size, fmt, quant, dims = rows[0]
            item = dict(bytes=int(size), format=int(fmt), quant=int(quant),
                        dims=[int(x) for x in dims.split('x')])
            require(item['format'] == item['quant'] == 0 and
                    math.prod(item['dims']) * 4 == item['bytes'], 'unreviewed native format/layout')
            result[kind].append(item)
    require('reference-status=0;' in log, 'runner did not finish cleanly')
    return result


def qualify(c):
    require(set(c) == {'encoder', 'decoder', 'joiner'}, 'missing model roles')
    enc, dec, join = (c[x] for x in ('encoder', 'decoder', 'joiner'))
    require(len(enc['input']) == len(enc['output']) == 36, 'not the general ASR encoder')
    require(enc['input'][0]['dims'] == [80, 103, 1] and
            enc['output'][0]['dims'] == [512, 24, 1], 'encoder interface changed')
    require(all(enc['input'][i] == enc['output'][i] for i in range(1, 36)), 'state layout changed')
    require([x['dims'] for x in dec['input']] == [[2, 1]] and
            [x['dims'] for x in dec['output']] == [[512, 1]], 'decoder interface changed')
    require([x['dims'] for x in join['input']] == [[512, 1], [512, 1]] and
            [x['dims'] for x in join['output']] == [[6254, 1]], 'joiner interface changed')


def idle():
    require(os.geteuid() == 0, 'root required for complete /proc NPU ownership check')
    target = os.stat('/dev/vipcore')
    owners = []
    for process in Path('/proc').iterdir():
        if not process.name.isdecimal():
            continue
        try:
            entries = list((process / 'fd').iterdir())
        except FileNotFoundError:
            continue
        for fd in entries:
            try:
                s = fd.stat()
            except FileNotFoundError:
                continue
            if stat.S_ISCHR(s.st_mode) and s.st_rdev == target.st_rdev:
                owners.append(str(fd))
    require(not owners, 'NPU owned by other process: ' + repr(owners))
    temps = {}
    for sensor in Path('/sys/class/thermal').glob('thermal_zone*/temp'):
        raw = sensor.read_text().strip()
        require(raw.isdecimal() and int(raw) < 75000, 'thermal status unsafe/unknown')
        temps[str(sensor)] = int(raw)
    require(temps, 'no thermal evidence')
    return temps


class Native:
    def __init__(self, base, work, contract):
        self.base, self.work, self.contract = base, work, contract
        self.calls = 0
        self.deadline = time.monotonic() + 900
        self.trace = (work / 'calls.jsonl').open('x', encoding='utf8')

    def call(self, role, inputs, label):
        require(time.monotonic() < self.deadline and self.calls < 512, 'test budget exhausted')
        require(re.fullmatch('[a-z0-9_-]+', label), 'unsafe call label')
        require(len(inputs) == len(self.contract[role]['input']), 'input count changed')
        temps = idle()
        folder = self.work / label
        folder.mkdir(mode=0o755)
        source = folder / 'input'
        source.mkdir(mode=0o755)
        for i, (data, desc) in enumerate(zip(inputs, self.contract[role]['input'])):
            require(len(data) == desc['bytes'], 'native input size changed')
            floats(data)
            write(source / ('input%d.bin' % i), data)
        self.calls += 1
        env = dict(os.environ, LD_LIBRARY_PATH=str(self.base / 'lib'))
        started = time.monotonic()
        with (folder / 'native.log').open('xb') as log:
            # Any failure stops the entire batch. No automatic reset/retry.
            status = subprocess.run(['timeout', '--signal=TERM', '--kill-after=5s', '20s',
                                     str(self.base / 'voice-native'), str(self.base / 'models' / (role + '.nb')),
                                     str(source), str(folder / 'output')],
                                    stdout=log, stderr=subprocess.STDOUT, env=env, timeout=28).returncode
        text = read(folder / 'native.log').decode('utf8', errors='strict')
        require(status == 0, 'native run failed (%s): preserve log; do not retry/reset' % label)
        require(metadata(text) == self.contract[role], 'runtime contract changed')
        timing = re.findall(r'^run-status=0 wall-us=(\d+)$', text, re.M)
        require(len(timing) == 1, 'missing hardware execution time')
        outputs = []
        for i, desc in enumerate(self.contract[role]['output']):
            data = read(folder / 'output' / ('output%d.bin' % i), desc['bytes'])
            floats(data)
            outputs.append(data)
        item = dict(role=role, label=label, temperature_mc=temps, npu_wall_us=int(timing[0]),
                    process_seconds=time.monotonic() - started,
                    input_sha256=[sha(x) for x in inputs], output_sha256=[sha(x) for x in outputs])
        self.trace.write(json.dumps(item, sort_keys=True) + '\n')
        self.trace.flush()
        print('NATIVE %s calls=%d npu-us=%s' % (label, self.calls, timing[0]), flush=True)
        return outputs

    def case(self, item, tokens):
        tag = item['case']
        features = read(self.base / 'fixtures' / tag / 'features.f32', item['feature_bytes'])
        require(sha(features) == item['feature_sha256'], 'feature fixture changed')
        floats(features)
        require(len(features) // 320 >= ((item['real_frames'] - 1) // 96) * 96 + 103, 'short fbank tail')
        states = [bytes(x['bytes']) for x in self.contract['encoder']['input'][1:]]
        context = [0, 0]
        decoder = self.call('decoder', [struct.pack('<2f', *context)], tag + '-initial-decoder')[0]
        chosen, positions, encoded = [], [], []
        started = time.monotonic()
        for chunk, start in enumerate(range(0, item['real_frames'], 96)):
            name = '%s-c%02d' % (tag, chunk)
            inputs = [features[start * 320:(start + 103) * 320], *states]
            output = self.call('encoder', inputs, name + '-encoder')
            if chunk == 0:
                again = self.call('encoder', inputs, name + '-repeat')
                require(output == again, 'native identical-input encoder repeat differs')
            encoded.append(sha(output[0]))
            states = output[1:]  # ALL 35 states, never CPU outputs or synthetic reset.
            for frame in range(24):
                namef = name + '-f%02d' % frame
                row = output[0][frame * 2048:(frame + 1) * 2048]
                logits = floats(self.call('joiner', [row, decoder], namef + '-joiner')[0])
                best = max(range(6254), key=logits.__getitem__)
                if best not in (0, 2):
                    require(best != 1 and len(chosen) < 1024, 'special token/token limit')
                    chosen.append(best)
                    positions.append(chunk * 24 + frame)
                    context = [context[-1], best]
                    decoder = self.call('decoder', [struct.pack('<2f', *context)], namef + '-decoder')[0]
            print('ASR_PARTIAL %s chunk=%d text=%s' %
                  (tag, chunk, ''.join(tokens[x] for x in chosen).replace('\u2581', ' ').strip()), flush=True)
        text = ''.join(tokens[x] for x in chosen).replace('\u2581', ' ').strip()
        # CPU exact tokens are an independent comparison, not an injected answer.
        result = dict(case=tag, text=text, token_ids=chosen, frame_positions=positions,
                      expected_cpu_text=item['text'], cpu_text_equal=text == item['text'],
                      cpu_tokens_equal=chosen == item['token_ids'],
                      cpu_positions_equal=positions == item['frame_positions'],
                      encoder_output_sha256=encoded, retained_native_state_count=35,
                      process_seconds=time.monotonic() - started, npu_executed=True,
                      rtOS_executed=False, full_voice_chain=False)
        record(self.work / (tag + '.json'), result)
        return result


def check(base):
    manifest = json.loads(read(base / 'manifest.json'))
    require(manifest['schema'] == 183 and manifest['full_voice_chain'] is False, 'wrong kit')
    for name, item in manifest['files'].items():
        path = Path(name)
        require(not path.is_absolute() and '..' not in path.parts, 'unsafe asset path')
        data = read(base / path, item['bytes'])
        require(sha(data) == item['sha256'], 'asset changed: ' + name)
    c = json.loads(read(base / 'contract.json'))
    qualify(c)
    cases = json.loads(read(base / 'fixtures.json'))
    require(len(cases) == 3 and len({c['case'] for c in cases}) == 3, 'need speech/time/silence cases')
    tokens = {}
    for row in read(base / 'tokens.txt').decode('utf8').splitlines():
        text, index = row.rsplit(maxsplit=1)
        index = int(index)
        require(index not in tokens, 'duplicate token')
        tokens[index] = text
    require(set(tokens) == set(range(6257)) and tokens[0] == '<blk>', 'wrong ASR vocabulary')
    return c, cases, tokens


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('base', type=Path)
    p.add_argument('--output', type=Path)
    p.add_argument('--run', action='store_true')
    p.add_argument('--npu-idle-confirmed', action='store_true')
    a = p.parse_args()
    base = a.base.resolve()
    contract, cases, tokens = check(base)
    if not a.run:
        print('PASS: private ASR assets checked; NO NPU EXECUTION')
        return
    require(a.npu_idle_confirmed and a.output is not None, 'explicit idle/output required')
    require(platform.machine() == 'aarch64' and stat.S_ISCHR(os.stat('/dev/vipcore').st_mode), 'not target Linux')
    a.output.mkdir(mode=0o755)
    import fcntl
    with (base / 'native-test.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        native = Native(base, a.output, contract)
        results = []
        try:
            for item in cases:
                results.append(native.case(item, tokens))
            success = all(x['cpu_text_equal'] and x['cpu_tokens_equal'] for x in results)
            record(a.output / 'summary.json', dict(result='NATIVE_GENERAL_ASR_TEXT_MATCH' if success else 'NATIVE_GENERAL_ASR_MISMATCH',
                                                  cases=results, calls=native.calls, npu_executed=True,
                                                  openvela_provider_ready=False, full_voice_chain=False))
            require(success, 'native transcript differs; preserve complete evidence')
        finally:
            native.trace.close()


if __name__ == '__main__':
    main()
