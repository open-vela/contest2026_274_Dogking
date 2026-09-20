#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Probe 6: big-file segment boundaries + tools.shell_command shape + session_meta count."""
import json
import os
import re

BIG = "/mnt/c/Users/hxy299/Desktop/2026/07/27/rollout-2026-07-27T10-08-16-019fa154-7ab3-7f31-b3cc-df6b6a829376.jsonl"

events = []          # (ordinal, type, subtype, total_tokens, ts, extra)
shell_samples = []
sess = []
with open(BIG, "r", encoding="utf-8", errors="replace") as fh:
    for line in fh:
        if '"token_count"' not in line and '"session_meta"' not in line and "shell_command" not in line:
            continue
        try:
            o = json.loads(line)
        except Exception:
            continue
        if not isinstance(o, dict):
            continue
        t, pl = o.get("type"), o.get("payload")
        if not isinstance(pl, dict):
            continue
        if t == "session_meta":
            sess.append((o.get("ordinal"), o.get("timestamp"), pl.get("id"), pl.get("session_id"),
                         pl.get("thread_source"), pl.get("cli_version"), str(pl.get("source"))[:80]))
        if t == "event_msg" and pl.get("type") == "token_count":
            tot = (pl.get("info") or {}).get("total_token_usage") or {}
            events.append((o.get("ordinal"), tot.get("total_tokens"), o.get("timestamp")))
        if "shell_command" in line and isinstance(pl.get("input"), str) and "shell_command" in pl.get("input"):
            for m in re.finditer(r"tools\.shell_command\s*\(", pl["input"]):
                shell_samples.append(pl["input"][max(0, m.start() - 80): m.start() + 400])
                break
            if len(shell_samples) >= 5:
                pass

print("### session_meta records in BIG file:", len(sess))
for s in sess:
    print("   ", s)

print("\n### shell_command samples in BIG file:", len(shell_samples))
for s in shell_samples[:5]:
    print("   ...", repr(s))

print("\n### token_count resets in BIG file")
prev = None
for (ordi, tt, ts) in events:
    if isinstance(tt, (int, float)):
        if prev is not None and tt < prev:
            print(f"   RESET at ordinal={ordi} prev={prev:,} -> new={tt:,} ts={ts}")
        prev = tt

print("\nfirst 3 events:", events[:3])
print("last 3 events:", events[-3:])
