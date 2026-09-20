#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Probe 5: shape of tools.shell_command(...) + token mechanism overlap validation."""
import json
import os
import re
import collections
import sys

ROOT = "/mnt/c/Users/hxy299/Desktop/2026"
BIG = os.path.join(ROOT, "07/27/rollout-2026-07-27T10-08-16-019fa154-7ab3-7f31-b3cc-df6b6a829376.jsonl")


def files():
    out = []
    for dp, _dn, fns in os.walk(ROOT):
        for fn in fns:
            if fn.endswith(".jsonl"):
                out.append(os.path.join(dp, fn))
    return sorted(out)


print("### tools.shell_command( call shapes")
seen = 0
for p in files():
    if os.path.getsize(p) > 30_000_000:
        continue
    with open(p, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            if "shell_command" not in line:
                continue
            try:
                o = json.loads(line)
            except Exception:  # noqa: BLE001
                continue
            pl = o.get("payload") if isinstance(o, dict) else None
            if not isinstance(pl, dict):
                continue
            inp = pl.get("input")
            if not isinstance(inp, str) or "shell_command" not in inp:
                continue
            for m in re.finditer(r"tools\.shell_command\s*\(", inp):
                print("   ...", repr(inp[max(0, m.start() - 60): m.start() + 420]))
                seen += 1
                break
            if seen >= 6:
                break
    if seen >= 6:
        break

print("\n### token mechanism overlap: tc_segment_sum.total_tokens vs tur_usage_sum.total_tokens")
D = json.load(open("/tmp/logstats.json", encoding="utf-8"))
for f in D["files"]:
    tc = f.get("tc_segment_sum", {}).get("total_tokens", 0)
    tur = f.get("tur_usage_sum", {}).get("total_tokens", 0)
    if f.get("token_count_events") and f.get("tur_events"):
        ratio = (tur / tc) if tc else float("nan")
        print(f"   tc={tc:>14,}  tur={tur:>14,}  tur/tc={ratio:6.3f}  tcev={f['token_count_events']:<5} turev={f['tur_events']:<5} {f['path'][:70]}")

print("\n### segment validation for multi-segment files (full rescan of token_count totals)")
for p in files():
    if os.path.getsize(p) < 4_000_000:
        continue
    seq = []
    with open(p, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            if '"token_count"' not in line:
                continue
            try:
                o = json.loads(line)
            except Exception:  # noqa: BLE001
                continue
            pl = o.get("payload") if isinstance(o, dict) else None
            if isinstance(pl, dict) and pl.get("type") == "token_count":
                tot = (pl.get("info") or {}).get("total_token_usage")
                if isinstance(tot, dict):
                    seq.append(tot.get("total_tokens"))
    segs, cur = [], None
    for v in seq:
        if not isinstance(v, (int, float)):
            continue
        if cur is None or v >= cur:
            cur = v
        else:
            segs.append(cur)
            cur = v
    if cur is not None:
        segs.append(cur)
    if len(segs) > 1:
        print(f"   {os.path.relpath(p, ROOT)}")
        print(f"      events={len(seq)} segments={len(segs)} values={segs} sum={sum(segs):,} last={segs[-1]:,}")
