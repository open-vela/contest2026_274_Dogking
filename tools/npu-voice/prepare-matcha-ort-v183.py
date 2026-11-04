#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Prepare a private portable minimal-ORT Matcha CPU artifact and oracle.

Official ORT conversion, basic optimizations, no NCHWc/host-specific packing.
Deterministic noise_scale=0 is used ONLY for conversion equivalence checks.
No target executor or audio/NPU/latency acceptance is asserted.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import time

REPO = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(REPO / 'out/ort-hostdeps-v183'),
                str(REPO.parent.parent / '.hostdeps-voice-batch'),
                str(REPO.parent.parent / '.hostdeps-v153')]
import numpy as np
import onnxruntime as ort
from onnxruntime.tools.convert_onnx_models_to_ort import OptimizationStyle, convert_onnx_models_to_ort


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    a = parser.parse_args()
    a.output.mkdir()
    model = REPO.parent / 'references/matcha-icefall-zh-baker/model-steps-3.onnx'
    assert sha(model) == 'ff1cc7f7abdedc4eeec668a4a0dc2e72a0c314c5637d5fce885345b490bd08de'
    assert ort.__version__ == '1.23.2'
    os.environ['ORT_CONVERT_ONNX_MODELS_TO_ORT_OPTIMIZATION_LEVEL'] = 'basic'
    convert_onnx_models_to_ort(model, output_dir=a.output,
                             optimization_styles=[OptimizationStyle.Fixed],
                             target_platform='arm', enable_type_reduction=False)
    converted = a.output / 'model-steps-3.basic.ort'
    config = a.output / 'model-steps-3.required_operators.basic.config'
    assert converted.is_file() and config.is_file()
    opts = ort.SessionOptions()
    opts.intra_op_num_threads = 4
    opts.inter_op_num_threads = 1
    original = ort.InferenceSession(str(model), opts, providers=['CPUExecutionProvider'])
    candidate = ort.InferenceSession(str(converted), opts, providers=['CPUExecutionProvider'])
    gold = json.loads((REPO / 'docs/evidence/local-voice-v179/host-speech-summary.json').read_text(encoding='utf8'))
    results = []
    for case in gold['tts']['cases'][:4]:
        for ids in case['ids']:
            x = np.asarray([ids], dtype=np.int64)
            feed = dict(x=x, x_length=np.asarray([len(ids)], dtype=np.int64),
                        noise_scale=np.asarray([0.0], dtype=np.float32),
                        length_scale=np.asarray([1.0], dtype=np.float32))
            start = time.monotonic()
            expected = original.run(None, feed)[0]
            middle = time.monotonic()
            actual = candidate.run(None, feed)[0]
            end = time.monotonic()
            assert expected.shape == actual.shape and expected.shape[:2] == (1, 80)
            assert np.isfinite(expected).all() and np.isfinite(actual).all()
            error = float(np.abs(expected - actual).max())
            assert error <= .005, (case['text'], error)
            results.append(dict(text=case['text'], ids=ids, mel_shape=list(actual.shape),
                                max_abs=error, original_host_seconds=middle-start,
                                minimal_format_host_seconds=end-middle))
    record = dict(result='MATCHA_MINIMAL_ORT_FORMAT_CPU_ORACLE_PASSED',
                  source_sha256=sha(model), ort_version=ort.__version__,
                  model=dict(path=converted.name, bytes=converted.stat().st_size, sha256=sha(converted)),
                  operators=dict(path=config.name, sha256=sha(config)),
                  conversion='basic/Fixed/arm; no NCHWc', cases=results,
                  license='Baker trained model non-commercial only; no model redistribution in Git',
                  target_cpu_executor_ready=False, npu_executed=False, full_voice_chain=False)
    with (a.output / 'summary.json').open('x', encoding='utf8') as f:
        json.dump(record, f, ensure_ascii=False, indent=2)
    print(json.dumps(record, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
