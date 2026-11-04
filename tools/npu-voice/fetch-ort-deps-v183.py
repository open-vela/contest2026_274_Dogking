#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Fetch CPU-only ORT dependencies using its fixed upstream SHA1 locks.

Downloads artifacts only; never executes or patches dependencies. New directory
only. HTTPS and bounded files/deadlines; partial files retained on failure.
The pinned original ORT source/mirror and system installations remain untouched.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import time
import urllib.request

NAMES = {'abseil_cpp', 'date', 'dlpack', 'eigen', 'flatbuffers', 'json', 'microsoft_gsl',
         'mp11', 'onnx', 'protobuf', 'protoc_linux_x64', 're2', 'safeint', 'pytorch_cpuinfo',
         'fp16', 'fxdiv', 'psimd', 'pthreadpool'}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path)
    p.add_argument('output', type=Path)
    a = p.parse_args()
    assert (a.source / 'VERSION_NUMBER').read_text().strip() == '1.23.2'
    a.output.mkdir()
    deps = [row.split(';') for row in (a.source / 'cmake/deps.txt').read_text().splitlines()
            if row and not row.startswith('#') and row.split(';')[0] in NAMES]
    assert {r[0] for r in deps} == NAMES
    def fetch(row):
        name, url, expected = row
        assert url.startswith('https://github.com/') and len(expected) == 40
        # Flat names avoid Windows MAX_PATH. Build staging reconstructs the
        # upstream mirror path under Linux; URL and both hashes stay recorded.
        path = a.output / (name + '.zip')
        partial = path.with_suffix(path.suffix + '.part')
        until = time.monotonic() + 180
        size = 0
        sha1, sha256 = hashlib.sha1(), hashlib.sha256()
        req = urllib.request.Request(url, headers={'User-Agent': 'Dogking-ORT-source-qualification/183'})
        with urllib.request.urlopen(req, timeout=45) as r, partial.open('xb') as f:
            assert r.url.startswith('https://')
            while True:
                assert time.monotonic() < until, 'dependency download deadline'
                data = r.read(1024 * 1024)
                if not data:
                    break
                size += len(data)
                assert size <= 128 * 1024 * 1024
                f.write(data)
                sha1.update(data)
                sha256.update(data)
        assert sha1.hexdigest() == expected, ('upstream SHA1 mismatch', name)
        assert not path.exists()
        partial.rename(path)
        item = dict(name=name, url=url, bytes=size, sha1=sha1.hexdigest(), sha256=sha256.hexdigest(),
                    archive=path.name, relative=url.removeprefix('https://'))
        print('DEPENDENCY_VERIFIED', name, size, flush=True)
        return item
    with ThreadPoolExecutor(max_workers=4) as pool:
        values = list(pool.map(fetch, deps))
    with (a.output / 'manifest.json').open('x') as f:
        json.dump(dict(ort_source_commit='a83fc4d58cb48eb68890dd689f94f28288cf2278', files=values), f, indent=2)


if __name__ == '__main__':
    main()
