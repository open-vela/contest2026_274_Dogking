#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Linux one-shot layout capture against qualified REAL native ASR goldens.

Uses nonzero recurrent state/context. Exactly three single-owner model calls,
no reset/retry, records actual child exit status and compares every output.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import time


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('kit', type=Path)
    p.add_argument('golden', type=Path)
    p.add_argument('runner', type=Path)
    p.add_argument('tracer', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--npu-idle-confirmed', action='store_true')
    a = p.parse_args()
    spec = importlib.util.spec_from_file_location('native_asr', Path(__file__).with_name('asr-native-v183.py'))
    n = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(n)
    n.require(a.npu_idle_confirmed, 'explicit idle confirmation required')
    contracts, _, _ = n.check(a.kit)
    golden = json.loads(n.read(a.golden / 'summary.json'))
    n.require(golden['result'] == 'NATIVE_GENERAL_ASR_TEXT_MATCH', 'unqualified native golden')
    a.output.mkdir(mode=0o755)
    labels = {'encoder': 'cmd_open_light_16k-c01-encoder',
              'decoder': 'cmd_open_light_16k-c00-f00-decoder',
              'joiner': 'cmd_open_light_16k-c00-f00-joiner'}
    calls = {r['label']: r for r in map(json.loads, (a.golden / 'calls.jsonl').read_text().splitlines())}
    summary = []
    deadline = time.monotonic() + 300
    for role in ('encoder', 'decoder', 'joiner'):
        n.require(time.monotonic() < deadline, 'total capture deadline')
        temperature = n.idle()
        label = labels[role]
        case = a.golden / label
        call = calls[label]
        for i, desc in enumerate(contracts[role]['input']):
            data = n.read(case / 'input' / ('input%d.bin' % i), desc['bytes'])
            n.require(n.sha(data) == call['input_sha256'][i], 'golden input changed')
        trace = a.output / (role + '-trace')
        trace.mkdir(mode=0o755)
        output = a.output / (role + '-output')
        env = dict(os.environ, LD_LIBRARY_PATH=str(a.kit.resolve() / 'lib'),
                   LD_PRELOAD=str(a.tracer.resolve()), VIP_TENSOR_TRACE_DIR=str(trace.resolve()))
        logpath = a.output / (role + '.log')
        command = ['timeout', '--signal=TERM', '--kill-after=5s', '60s',
                   str(a.runner.resolve()), str(a.kit.resolve() / 'models' / (role + '.nb')),
                   str(case.resolve() / 'input'), str(output.resolve())]
        with logpath.open('xb') as log:
            code = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=68).returncode
            log.write(('process-exit=%d tee-exit=0\n' % code).encode())
        n.require(code == 0, 'capture failed; preserve logs, no reset/retry')
        n.require(n.metadata(n.read(logpath).decode()) == contracts[role], 'capture contract drift')
        for i, desc in enumerate(contracts[role]['output']):
            data = n.read(output / ('output%d.bin' % i), desc['bytes'])
            n.require(n.sha(data) == call['output_sha256'][i], 'tracing changed native output')
        records = [json.loads(row) for row in (trace / 'trace.jsonl').read_text().splitlines()]
        final = [r for r in records if r.get('kind') == 'final']
        n.require(len(final) == 1 and final[0]['failed'] == 0 and final[0]['snapshots'] == 2,
                  'incomplete/failed trace')
        n.require(all(r['rc'] == r['error'] == 0 for r in records if r.get('kind') == 'return'),
                  'native ioctl failure')
        summary.append(dict(role=role, native_exit=code, all_outputs_equal=True,
                            golden_label=label, temperature_mc=temperature,
                            trace_bytes=final[0]['dumped_bytes']))
        print('LAYOUT', role, 'all native outputs match', flush=True)
    n.record(a.output / 'summary.json', dict(result='GENERAL_ASR_NATIVE_LAYOUT_CAPTURED',
                                            models=summary, openvela_executed=False,
                                            full_voice_chain=False))


if __name__ == '__main__':
    main()
