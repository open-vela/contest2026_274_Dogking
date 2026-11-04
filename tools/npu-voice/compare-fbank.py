#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Read-only finite F32 numerical comparison. Never certifies NPU/ASR accuracy."""
import argparse
import hashlib
import json
import math
import struct
import zlib
from pathlib import Path


def load(path):
    size = path.stat().st_size
    if not 29 * 320 <= size <= 1000 * 320 or size % 320:
        raise ValueError('Expected 29..1000 frames, 80 little-endian F32 bins')
    data = path.read_bytes()
    if len(data) != size:
        raise ValueError('File changed size while reading')
    values = struct.unpack('<%df' % (len(data) // 4), data)
    if not all(math.isfinite(x) and abs(x) <= 1000 for x in values):
        raise ValueError('Nonfinite/out-of-domain feature')
    return data, values


def compare(reference, actual):
    rb, rv = load(reference)
    ab, av = load(actual)
    if len(rb) != len(ab):
        raise ValueError('Frame/length mismatch')
    different = [i for i in range(len(rv)) if rb[i*4:i*4+4] != ab[i*4:i*4+4]]
    errors = [abs(x-y) for x, y in zip(rv, av)]
    first = different[0] if different else None
    def identity(data):
        return dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
                    raw_crc32='%08x' % ((zlib.crc32(data, 0xffffffff) ^ 0xffffffff) & 0xffffffff))
    return dict(schema=163, byte_exact=rb == ab, frames=len(rb)//320, bins=80,
                differing=len(different), elements=len(rv), maximum_absolute=max(errors),
                rmse=math.sqrt(math.fsum(x*x for x in errors)/len(errors)),
                first=None if first is None else dict(frame=first//80, bin=first%80,
                    reference=rv[first], actual=av[first]),
                reference=identity(rb), actual=identity(ab),
                hardware_executed=False, acoustic_accuracy_accepted=False,
                note='File comparison only; input origin is not certified. No tolerance promotion or oracle rewrite.')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('reference', type=Path)
    p.add_argument('actual', type=Path)
    p.add_argument('--json', type=Path, help='Optional NEW report (never overwrite)')
    a = p.parse_args()
    try:
        result = compare(a.reference, a.actual)
        encoded = json.dumps(result, indent=2, allow_nan=False) + '\n'
        if a.json:
            with a.json.open('x', encoding='utf-8') as stream:
                stream.write(encoded)
        print(encoded, end='')
        raise SystemExit(0 if result['byte_exact'] else 2)
    except (OSError, ValueError) as error:
        p.exit(1, 'STOP: ' + str(error) + '\n')
