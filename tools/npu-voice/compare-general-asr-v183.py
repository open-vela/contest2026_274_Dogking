#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Independent CPU ONNX comparison for EVERY captured native ASR output.

Native inputs drive each CPU call, including all 35 actual recurrent states.
FP16-vs-FP32 errors are reported, not concealed or treated as byte equality.
No NPU/target execution or prepared package export happens here.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sys

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO.parent.parent / '.hostdeps-voice-batch'))
import numpy as np
import onnxruntime as ort


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('report', type=Path)
    p.add_argument('output', type=Path)
    a = p.parse_args()
    native = load('native', 'asr-native-v183.py')
    lock = load('locked_asr', 'fetch-asr-source.py')
    source = REPO.parent / 'references/zipformer-asr-upstream-e238275'
    lock.verify(source)
    options = ort.SessionOptions()
    options.intra_op_num_threads = 4
    options.inter_op_num_threads = 1
    sessions = {r: ort.InferenceSession(str(source / next(n for n in lock.FILES if n.startswith(r))),
                                       options, providers=['CPUExecutionProvider'])
                for r in ('encoder', 'decoder', 'joiner')}
    summary = json.loads(native.read(a.report / 'summary.json'))
    assert summary['result'] == 'NATIVE_GENERAL_ASR_TEXT_MATCH'
    calls = [json.loads(row) for row in (a.report / 'calls.jsonl').read_text().splitlines()]
    assert len(calls) == summary['calls'] == 216
    a.output.mkdir()
    all_metrics = []
    for call in calls:
        role, label = call['role'], call['label']
        assert role in sessions and re.fullmatch('[a-z0-9_-]+', label)
        s = sessions[role]
        folder = a.report / label
        native_inputs = [native.read(folder / 'input' / ('input%d.bin' % i)) for i in range(len(s.get_inputs()))]
        assert [native.sha(x) for x in native_inputs] == call['input_sha256']
        feed = {}
        for item, raw in zip(s.get_inputs(), native_inputs):
            values = np.frombuffer(raw, dtype='<f4')
            shape = [1 if d == 'N' else d for d in item.shape]
            assert all(isinstance(d, int) and d > 0 for d in shape)
            assert int(np.prod(shape)) == values.size
            if item.type == 'tensor(int64)':
                assert np.isfinite(values).all() and (values == np.rint(values)).all()
                values = values.astype('<i8')
            else:
                assert item.type == 'tensor(float)'
            feed[item.name] = values.reshape(shape)
        expected = s.run(None, feed)
        for i, cpu in enumerate(expected):
            raw = native.read(folder / 'output' / ('output%d.bin' % i))
            assert native.sha(raw) == call['output_sha256'][i]
            got = np.frombuffer(raw, dtype='<f4').astype(np.float64)
            reference = cpu.reshape(-1).astype(np.float64)
            assert got.size == reference.size and np.isfinite(got).all() and np.isfinite(reference).all()
            diff = got - reference
            denom = max(float(np.linalg.norm(reference)), 1e-12)
            entry = dict(label=label, role=role, output=i, count=int(got.size),
                         max_abs=float(np.abs(diff).max()), rms=float(np.sqrt(np.mean(diff * diff))),
                         relative_l2=float(np.linalg.norm(diff)) / denom,
                         native_sha256=call['output_sha256'][i],
                         cpu_sha256=hashlib.sha256(cpu.tobytes()).hexdigest(),
                         integer_exact=bool(np.array_equal(got, reference)) if cpu.dtype.kind == 'i' else None)
            if role == 'joiner':
                entry['argmax_equal'] = int(got.argmax()) == int(reference.argmax())
            all_metrics.append(entry)
        print('CPU_COMPARE', label, 'outputs', len(expected), flush=True)
    native.record(a.output / 'tensor-comparison.json', all_metrics)
    roles = {}
    for role in sessions:
        rows = [r for r in all_metrics if r['role'] == role]
        roles[role] = dict(outputs=len(rows), max_abs=max(r['max_abs'] for r in rows),
                           max_relative_l2=max(r['relative_l2'] for r in rows),
                           integer_exact=all(r['integer_exact'] is not False for r in rows),
                           argmax_mismatches=sum(r.get('argmax_equal') is False for r in rows))
    result = dict(result='ALL_NATIVE_OUTPUTS_COMPARED_WITH_CPU_ONNX', calls=len(calls),
                  outputs=len(all_metrics), roles=roles, onnxruntime=ort.__version__,
                  native_text_tokens_positions_equal=all(c['cpu_text_equal'] and c['cpu_tokens_equal'] and
                                                         c['cpu_positions_equal'] for c in summary['cases']),
                  float_tolerance_accepted=False, openvela_executed=False,
                  full_voice_chain=False)
    native.record(a.output / 'summary.json', result)
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
