#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Offline full-byte audit of private prepared Linux results; never calls NPU.

KWS mode also re-runs the same application C search on host, using ONLY the
freshly returned prepared tensors. A native host search library is required.
"""
import argparse
import hashlib
import importlib.util
import json
import struct
from pathlib import Path


def require(ok, message):
    if not ok:
        raise ValueError(message)


def load(path):
    return json.loads(path.read_text())


def sha(data):
    return hashlib.sha256(data).hexdigest()


def tensor(path, size, digest):
    require(path.is_file() and not path.is_symlink(), 'Missing/nonregular tensor: ' + str(path))
    data = path.read_bytes()
    require(len(data) == size and sha(data) == digest, 'Tensor differs: ' + str(path))
    return data


def models(kit):
    index = load(kit / 'export-index.json')
    result = {m['model']: m for m in index['models']}
    require(len(result) == 4, 'Four model contracts required')
    for m in result.values():
        tensor(kit / m['package'], m['package_bytes'], m['package_sha256'])
    return result


def checkpoints(args):
    ms = models(args.kit)
    fixed, dynamic = load(args.fixed / 'summary.json'), load(args.dynamic / 'summary.json')
    require(fixed['result'] == 'A7TV_LINUX_FIXED_GOLDENS_PASSED', 'Fixed result')
    require(dynamic['result'] == 'A7TV_LINUX_DYNAMIC_GOLDENS_PASSED', 'Dynamic result')
    count = links = 0
    for name, m in ms.items():
        for i, e in enumerate(m['outputs']):
            tensor(args.fixed / (name + '-output') / ('output%d.bin' % i), e['bytes'], e['sha256'])
            count += 1
    cases = load(args.kit / 'dynamic-cases.json')['cases']
    require(len(cases) == len(dynamic['cases']) == 14, 'Fourteen dynamic cases required')
    previous = None
    for c in cases:
        for i, e in enumerate(c['inputs']):
            raw = tensor(args.dynamic / (c['id'] + '-input') / ('input%d.bin' % i), e['bytes'], e['sha256'])
            if c['model'] == 'encoder' and previous and 1 <= i <= 37:
                prior = args.dynamic / (previous + '-output') / ('output%d.bin' % i)
                require(raw == prior.read_bytes(), 'State feedback broken')
                links += 1
        for i, e in enumerate(c['outputs']):
            tensor(args.dynamic / (c['id'] + '-output') / ('output%d.bin' % i), e['bytes'], e['sha256'])
            count += 1
        if c['model'] == 'encoder':
            previous = c['id']
    require(count == 170 and links == 74, 'Checkpoint coverage changed')
    return dict(result='OFFLINE_FULL_BYTES_PASS', outputs=count, encoder_feedback_links=links,
                openvela_executed=False)


def kws(args):
    ms = models(args.kit)
    index = load(args.kit / 'kws-cases.json')
    reported = load(args.report / 'summary.json')
    require(reported['result'] == 'A7TV_LINUX_KWS_SIX_FIXTURES_PASSED' and
            reported['frozen_outputs_used'] is False, 'KWS result/claims')
    rows = [json.loads(line) for line in (args.report / 'tensor-trace.jsonl').read_text().splitlines()]
    require(len(rows) == reported['calls'], 'Call trace incomplete')
    returned, counts, total = {}, {}, 0
    for n, row in enumerate(rows, 1):
        require(row['call'] == n, 'Call sequence')
        name = row['model']; m = ms[name]; prefix = 'call%05d' % n
        key = name + ':' + ','.join(row['inputs'])
        require(key in index['oracles'] and row['outputs'] == index['oracles'][key], 'Native oracle differs')
        require(len(row['inputs']) == len(m['inputs']) and len(row['outputs']) == len(m['outputs']), 'Tensor count')
        for i, (digest, e) in enumerate(zip(row['inputs'], m['inputs'])):
            tensor(args.report / (prefix + '-input') / ('input%d.bin' % i), e['bytes'], digest)
        values = []
        for i, (digest, e) in enumerate(zip(row['outputs'], m['outputs'])):
            values.append(tensor(args.report / (prefix + '-output') / ('output%d.bin' % i), e['bytes'], digest))
            total += 1
        require(key not in returned or values == returned[key], 'Repeated input nondeterministic')
        returned[key] = values; counts[name] = counts.get(name, 0) + 1
    spec = importlib.util.spec_from_file_location('host_search', Path(__file__).with_name('run-kws-search.py'))
    search = importlib.util.module_from_spec(spec); spec.loader.exec_module(search)
    vocabulary = {l.rsplit(maxsplit=1)[0]: int(l.rsplit(maxsplit=1)[1])
                  for l in (args.kit / 'tokens.txt').read_text().splitlines()}
    words = search.keywords((args.kit / 'keywords.txt').read_text(), vocabulary)

    def query(name, inputs):
        key = name + ':' + ','.join(sha(raw) for raw in inputs)
        require(key in returned, 'Missing actual prepared output: ' + name)
        return returned[key]

    results = []; links = 0
    for c in index['cases']:
        features = (args.kit / c['kit_features']).read_bytes()
        require(sha(features) == c['feature_sha256'], 'Feature fixture changed')
        states = [bytes(e['bytes']) for e in ms['encoder']['inputs']]
        core = search.Search(args.library, words); hits = []
        for chunk in range(c['chunks']):
            offset = chunk * 16
            states[0] = features[offset*320:(offset+29)*320]; states[38] = struct.pack('<f', offset)
            outputs = query('encoder', states); states = [b''] + outputs[1:]
            if chunk: links += 37
            for frame in range(4):
                enc = outputs[0][frame*1280:(frame+1)*1280]
                logits = [query('joiner', [enc, query('decoder', [struct.pack('<2f', *ctx)])[0]])[0]
                          for ctx in core.contexts()]
                hit = core.advance(logits)
                if hit: hits.append(hit)
        actual = next(x for x in reported['cases'] if x['scenario'] == c['tag'])
        require(hits == actual['hits'], 'Host C search differs: ' + c['tag'])
        names = [h['phrase'] for h in hits]
        require(c['expected'] in names if c['expected'] else not names, 'Classification differs')
        results.append(dict(scenario=c['tag'], hits=hits))
    require(len(results) == 6, 'Six fixtures required')
    return dict(result='OFFLINE_FULL_BYTES_AND_C_SEARCH_PASS', calls=counts,
                outputs=total, encoder_feedback_links=links, cases=results,
                no_frozen_outputs=True, openvela_executed=False)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__); sub = p.add_subparsers(dest='mode', required=True)
    a = sub.add_parser('checkpoints')
    for name in ('kit', 'fixed', 'dynamic'): a.add_argument(name, type=Path)
    a = sub.add_parser('kws')
    for name in ('kit', 'report', 'library'): a.add_argument(name, type=Path)
    args = p.parse_args()
    print(json.dumps(checkpoints(args) if args.mode == 'checkpoints' else kws(args), ensure_ascii=False, indent=2))
