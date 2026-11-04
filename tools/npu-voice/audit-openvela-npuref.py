#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check a complete serial/SSH npuref transcript against private CRC oracles.

This checks supplied log content, not provenance, acoustic quality, reset
recovery or the full voice assistant. Never connects to or controls a board.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path

HEADER = re.compile(r'npuref: model=(\w+) backend=VIP2 inputs=(\d+) outputs=(\d+) '
                    r'mode=(captured-golden|dynamic-byte-check) elapsed-us=(\d+)')
OUTPUT = re.compile(r'npuref output\[(\d+)\] bytes=(\d+) raw-crc32=([0-9a-fA-F]{8})')


def require(ok, message):
    if not ok:
        raise ValueError(message)


def audit(expected, transcript, fixed_only=False):
    cases = expected['cases']
    fixed = {c['model']: c for c in cases if c['case'] == 'fixed'}
    require(set(fixed) == {'joiner', 'decoder', 'encoder', 'vocoder'}, 'Four fixed references required')
    ordered = [fixed[name] for name in ('joiner', 'decoder', 'encoder', 'vocoder')]
    if not fixed_only:
        ordered += [c for c in cases if c['case'] != 'fixed']
        require(len(ordered) == 18, 'Need fixed + fourteen dynamic references')
    active = None
    rows = []
    for line in transcript.splitlines():
        require('npuref: failed=' not in line, 'Execution failure in transcript; stop and diagnose')
        head, output = HEADER.search(line), OUTPUT.search(line)
        if head:
            if active is not None:
                require(len(active['outputs']) == active['count'], 'Previous outputs incomplete')
            active = dict(model=head[1], inputs=int(head[2]), count=int(head[3]), mode=head[4], outputs=[])
            rows.append(active)
        elif output:
            require(active is not None, 'Output before model header')
            require(int(output[1]) == len(active['outputs']), 'Output order/duplicate/gap')
            active['outputs'].append(dict(bytes=int(output[2]), raw_crc32=output[3].lower()))
    require(len(rows) == len(ordered), 'Run count differs or transcript truncated')
    count = 0
    for row, case in zip(rows, ordered):
        require(row['model'] == case['model'], 'Model order differs from TEST_COMMANDS.txt')
        require(row['inputs'] == (39 if row['model'] == 'encoder' else 2 if row['model'] == 'joiner' else 1),
                'Input contract differs')
        require(row['mode'] == ('captured-golden' if case['case'] == 'fixed' else 'dynamic-byte-check'), 'Mode differs')
        want = [dict(bytes=e['bytes'], raw_crc32=e['raw_crc32']) for e in case['outputs']]
        require(row['count'] == len(want) and row['outputs'] == want, 'Output bytes/CRC differ: ' + case['case'])
        count += len(want)
    return dict(result='NPUREF_SUPPLIED_LOG_CRCS_MATCH', runs=len(rows), outputs=count,
                log_only=True, acoustic_or_full_assistant_acceptance=False)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('expected', type=Path); p.add_argument('transcript', type=Path)
    p.add_argument('--fixed-only', action='store_true'); a = p.parse_args()
    try:
        expected_bytes, log_bytes = a.expected.read_bytes(), a.transcript.read_bytes()
        result = audit(json.loads(expected_bytes), log_bytes.decode('utf-8-sig', errors='strict'), a.fixed_only)
        result.update(expected_sha256=hashlib.sha256(expected_bytes).hexdigest(),
                      transcript_sha256=hashlib.sha256(log_bytes).hexdigest())
        print(json.dumps(result, indent=2))
    except (OSError, ValueError, KeyError) as e:
        p.exit(2, 'STOP: ' + str(e) + '\n')
