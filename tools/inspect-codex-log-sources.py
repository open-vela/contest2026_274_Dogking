#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Read-only inventory of explicitly supplied Codex JSONL/Zstandard snapshots.

Does not print dialogue or credentials, change source bytes or invent sessions.
"""
import argparse
import collections
import hashlib
import io
import json
from pathlib import Path
import zstandard


def lines(path):
    with path.open('rb') as source:
        if path.suffix == '.zst':
            with zstandard.ZstdDecompressor().stream_reader(source) as decoded:
                yield from io.TextIOWrapper(decoded, encoding='utf-8')
        else:
            yield from io.TextIOWrapper(source, encoding='utf-8')


def inspect(source_root, workspace, report):
    rows = []
    for path in sorted(source_root.rglob('*')):
        if not path.is_file():
            continue
        raw_sha = hashlib.sha256()
        with path.open('rb') as stream:
            for block in iter(lambda: stream.read(4 * 1024 * 1024), b''):
                raw_sha.update(block)
        it = iter(lines(path))
        first = json.loads(next(it))
        meta = first.get('payload', {})
        cwd = meta.get('cwd', '')
        selected = Path(cwd).resolve().is_relative_to(workspace.resolve()) if cwd else False
        row = dict(path=path.relative_to(source_root).as_posix(), bytes=path.stat().st_size,
                   sha256=raw_sha.hexdigest(), session_id=meta.get('id') or meta.get('session_id'),
                   selected=selected, cwd=cwd, first_timestamp=first.get('timestamp'),
                   first_ordinal=first.get('ordinal'), source_kind=meta.get('source'))
        row['first_records'] = []
        for i, line in enumerate(it):
            if i >= 8:
                break
            data = json.loads(line)
            row['first_records'].append({k: data.get(k) for k in ('type', 'timestamp', 'ordinal')})
        rows.append(row)
    counts = collections.Counter(r['session_id'] for r in rows if r['selected'])
    result = dict(files=len(rows), bytes=sum(r['bytes'] for r in rows),
                  selected_files=sum(r['selected'] for r in rows),
                  excluded_files=sum(not r['selected'] for r in rows),
                  selected_session_ids=len(counts),
                  duplicate_session_ids={k: v for k, v in counts.items() if v > 1}, files_inventory=rows)
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: v for k, v in result.items() if k != 'files_inventory'}, indent=2))
    for row in rows:
        if row['selected'] and counts[row['session_id']] > 1:
            print(json.dumps({k: row[k] for k in ('path', 'session_id', 'bytes',
                                                  'first_timestamp', 'first_records')}, ensure_ascii=False))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--workspace', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    inspect(args.source, args.workspace, args.report)
