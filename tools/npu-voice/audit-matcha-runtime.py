#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Read-only audit of the locked Matcha CPU graph's RTOS requirements.
No conversion, runtime execution, package install, network or target access.
"""
import argparse
import collections
import hashlib
import json
import sys
from pathlib import Path

repo = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(repo.parent.parent / '.hostdeps-v153'))
import onnx

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('model', type=Path)
a = p.parse_args()
expected = 'ff1cc7f7abdedc4eeec668a4a0dc2e72a0c314c5637d5fce885345b490bd08de'
if a.model.is_symlink() or not a.model.is_file() or a.model.stat().st_size > 256 * 1024 * 1024:
    raise ValueError('Invalid model file')
h = hashlib.sha256()
with a.model.open('rb') as f:
    for block in iter(lambda: f.read(4 * 1024 * 1024), b''): h.update(block)
if h.hexdigest() != expected: raise ValueError('Unqualified acoustic source hash')
m = onnx.load(str(a.model), load_external_data=False)

def describe(v):
    t = v.type.tensor_type
    return {'name': v.name, 'type': onnx.TensorProto.DataType.Name(t.elem_type),
            'shape': [d.dim_value if d.HasField('dim_value') else d.dim_param for d in t.shape.dim]}

ops = collections.Counter(); nodes = 0; external = 0; raw_bytes = 0; subgraphs = 0
def walk(graph):
    global nodes, external, raw_bytes, subgraphs
    for value in graph.initializer:
        external += value.data_location == onnx.TensorProto.EXTERNAL
        raw_bytes += len(value.raw_data)
    for n in graph.node:
        nodes += 1; ops[(n.domain + '::' if n.domain else '') + n.op_type] += 1
        for attr in n.attribute:
            if attr.type == onnx.AttributeProto.GRAPH:
                subgraphs += 1; walk(attr.g)
            elif attr.type == onnx.AttributeProto.GRAPHS:
                for g in attr.graphs: subgraphs += 1; walk(g)
walk(m.graph)
if external: raise ValueError('External model tensors need a separate locked contract')
result = {'model_sha256': expected, 'model_bytes': a.model.stat().st_size,
          'onnx_version': onnx.__version__, 'opset': {v.domain: v.version for v in m.opset_import},
          'inputs': [describe(v) for v in m.graph.input], 'outputs': [describe(v) for v in m.graph.output],
          'nodes': nodes, 'subgraphs': subgraphs, 'operators': dict(sorted(ops.items())),
          'random_ops': {k: v for k, v in ops.items() if 'Random' in k},
          'control_flow': {k: v for k, v in ops.items() if k in ('Loop', 'If', 'Scan')},
          'raw_initializer_bytes': raw_bytes, 'external_initializers': external,
          'static_only': True, 'maximum_runtime_memory_known': False,
          'target_cpu_executor_available': False, 'target_tts_ready': False,
          'note': 'An operator list is not an implementation. Dynamic shapes, bounded durations, allocation, random state and live-tensor lifetimes still need a real RTOS CPU engine.'}
print(json.dumps(result, ensure_ascii=False, indent=2))
