#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Explicit-directory import using pinned official event/redaction/writer functions.

Not a replacement collector. Never invent session IDs, combine resumptions,
rewrite exported events, install hooks or publish. Ambiguous and private sources
remain outside logs/. Dependencies: zstandard; official validator: jsonschema.
"""
import argparse
import collections
import hashlib
import importlib.util
import json
import re
import sys
from pathlib import Path
_helpers_spec = importlib.util.spec_from_file_location(
    'codex_source_inspector', Path(__file__).with_name('inspect-codex-log-sources.py'))
_helpers = importlib.util.module_from_spec(_helpers_spec)
_helpers_spec.loader.exec_module(_helpers)
lines = _helpers.lines


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(4 * 1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--inventory', type=Path, required=True)
    parser.add_argument('--collector', type=Path, required=True)
    parser.add_argument('--dest', type=Path, required=True)
    parser.add_argument('--audit', type=Path, required=True)
    parser.add_argument('--latest-resume-only', action='store_true',
                        help='Select one complete latest source snapshot per thread; never join shards')
    parser.add_argument('--most-text-snapshot', action='store_true',
                        help='Select one unmodified snapshot with most official-supported text events, never join shards')
    parser.add_argument('--base-manifest', type=Path,
                        help='Prior official session counts used only to continue seq without rewriting old events')
    parser.add_argument('--confirm', action='store_true')
    args = parser.parse_args()
    if not args.confirm or args.dest.exists():
        raise SystemExit('STOP: --confirm and a new empty destination are required')
    shared = args.collector / 'skills/contest-log-collector/adapters/shared'
    if sha(shared / 'snapshot_core.py') != '72b48d69b5fd52f1986030bf83f58c5c8f9beaa0784e7801786cc04f1811a87d':
        raise SystemExit('STOP: official parser differs from reviewed pinned source')
    sys.path.insert(0, str(shared.resolve()))
    spec = importlib.util.spec_from_file_location('official_snapshot_core', shared / 'snapshot_core.py')
    core = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(core)
    source_inventory = json.loads(args.inventory.read_text(encoding='utf-8'))
    rows = source_inventory['files_inventory']
    counts = collections.Counter(r['session_id'] for r in rows if r['selected'])
    latest = {}
    for row in rows:
        if row['selected'] and (row['session_id'] not in latest or
                row['first_timestamp'] > latest[row['session_id']]['first_timestamp']):
            latest[row['session_id']] = row
    snapshot_scores = []
    if args.most_text_snapshot:
        for sid, count in counts.items():
            if count <= 1:
                continue
            candidates = []
            for row in rows:
                if not row['selected'] or row['session_id'] != sid:
                    continue
                state, supported = {}, 0
                try:
                    for line in lines(args.source / row['path']):
                        raw = json.loads(line)
                        supported += len(core.expand_codex_rollout_line(raw, raw.get('timestamp', ''), state))
                except (ValueError, OSError):
                    supported = -1
                snapshot_scores.append(dict(source=row['path'], text_events=supported))
                candidates.append((supported, row['first_timestamp'], row))
            latest[sid] = max(candidates, key=lambda candidate: candidate[:2])[2]
            print(f'selected one original snapshot for {sid}: {max(c[0] for c in candidates)} text events', flush=True)
    base_counts = {}
    if args.base_manifest:
        base_counts = {r['session_id']: r['event_count'] for r in
                       json.loads(args.base_manifest.read_text(encoding='utf-8'))['sessions']}
    team, login = 'contest2026_274_Dogking', 'hxy299'
    member = args.dest / 'logs' / login
    manifest = core.read_manifest(member, team, login, 'codex')
    report = dict(official_repository='https://github.com/open-vela/.claude',
                  official_commit='ac36068f8571b2cced14c5f23f6cebb47026d1c6',
                  parser_sha256=sha(shared / 'snapshot_core.py'),
                  source_files=len(rows), accepted=[], excluded=[],
                  raw_sources_changed=False, exported_events_hand_edited=False,
                  note='Explicit user-selected directory; official library calls, not default auto-hook/backfill')
    report['snapshot_scores'] = snapshot_scores
    patterns = {
        'private-key': re.compile(r'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----'),
        'unredacted-token': re.compile(r'\b(?:sk-[A-Za-z0-9_-]{20,}|ghp_[A-Za-z0-9]{36})\b'),
        'credential-assignment': re.compile(
            r'(?i)\b(?:api[_-]?key|api[_-]?secret|password|passwd)\b\s*[:=]\s*[\"\x27]([A-Za-z0-9/+_.=-]{16,})[\"\x27]'),
    }
    args.dest.mkdir(parents=True)
    for row in rows:
        relative, sid = row['path'], row['session_id']
        kind = row.get('source_kind')
        guardian = isinstance(kind, dict) and kind.get('subagent', {}).get('other') == 'guardian'
        reason = 'outside-selected-workspace' if not row['selected'] else (
            'internal-guardian-review-not-development-dialogue' if guardian else (
            'nonselected-resume-snapshot-not-joined' if (args.latest_resume_only or args.most_text_snapshot) and row is not latest[sid] else (
            'ambiguous-resumed-session-shards' if counts[sid] != 1 and not (args.latest_resume_only or args.most_text_snapshot) else None)))
        if reason:
            report['excluded'].append(dict(source=relative, reason=reason))
            continue
        source = (args.source / relative).resolve()
        if not source.is_relative_to(args.source.resolve()):
            raise SystemExit('STOP: inventory path escapes selected source root')
        if sha(source) != row['sha256']:
            raise SystemExit('STOP: source changed since inventory: ' + relative)
        state, events, raw_count, parse_error = {}, [], 0, False
        try:
            for line in lines(source):
                if not line.strip():
                    continue
                raw = json.loads(line)
                raw_count += 1
                if raw.get('type') == 'session_meta':
                    meta = raw.get('payload') or {}
                    if (meta.get('id') or meta.get('session_id')) != sid:
                        raise ValueError('session identity mismatch')
                events.extend(core.expand_codex_rollout_line(raw, raw.get('timestamp', ''), state))
        except (ValueError, OSError) as exc:
            parse_error = type(exc).__name__
        if parse_error or not events:
            report['excluded'].append(dict(source=relative,
                reason='malformed-source-' + str(parse_error) if parse_error else 'no-supported-text-events'))
            continue
        redacted_preview = core.redact_value(events, core.DEFAULT_REDACT_RULES, [0])
        text = '\n'.join(e.get('text', '') for e in redacted_preview)
        findings = [kind for kind, pattern in patterns.items() if pattern.search(text)]
        if findings:
            report['excluded'].append(dict(source=relative, reason='privacy-review-required', findings=findings))
            continue
        date = events[0]['ts'][:10]
        if not re.fullmatch(r'\d{4}-\d{2}-\d{2}', date):
            report['excluded'].append(dict(source=relative, reason='missing-original-timestamp'))
            continue
        rel = f'logs/{login}/{date}/codex__{sid}.jsonl'
        output = args.dest / rel
        seq_start = base_counts.get(sid, 0)
        result = core.append_events(output, events, sid, team, login, 'codex', seq_start, core.DEFAULT_REDACT_RULES)
        if sha(source) != row['sha256']:
            raise SystemExit('STOP: source changed during import: ' + relative)
        manifest['sessions'].append(dict(
            session_id=sid, tool='codex', started_at=events[0]['ts'], last_event_at=events[-1]['ts'],
            event_count=result['written'], raw_event_count=raw_count, file_path=rel,
            collection_mode='vscode_extension_partial', health='degraded',
            data_completeness_warning='Codex desktop explicit selected-directory import; one original snapshot only, other resumptions NOT joined; selected by official-supported text count when requested; parser omits tool/reasoning/image; seq continues prior official count; NOT complete-history or auto-hook capture.',
            imported_seq_start=seq_start,
            redacted_count_total=result['redacted'],
            source_integrity=dict(main_sha256=row['sha256'], main_size=row['bytes'], captured_at=core.iso_now())))
        report['accepted'].append(dict(source=relative, session_id=sid, file=rel,
            events=result['written'], redacted=result['redacted'], bytes=output.stat().st_size,
            seq_start=seq_start, sha256=sha(output), source_sha256=row['sha256']))
        print(f'exported {len(report["accepted"])}: {sid} events={result["written"]}', flush=True)
    member.mkdir(parents=True, exist_ok=True)
    core.write_manifest(member, manifest, team, login, 'codex')
    report['accepted_sessions'] = len(report['accepted'])
    report['accepted_events'] = sum(r['events'] for r in report['accepted'])
    report['accepted_bytes'] = sum(r['bytes'] for r in report['accepted'])
    report['excluded_reasons'] = dict(collections.Counter(r['reason'] for r in report['excluded']))
    args.audit.parent.mkdir(parents=True, exist_ok=True)
    args.audit.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: v for k, v in report.items() if k not in ('accepted', 'excluded', 'snapshot_scores')}, indent=2))


if __name__ == '__main__':
    main()
