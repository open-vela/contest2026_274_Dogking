#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Probe 4: debug cmd extraction + token counter resets."""
import json
import re
import sys

sys.path.insert(0, "/mnt/d/My-Program-Files/all-Files-Openvela/hxy299-a733-share-hub-a733-cubie-a7z-share/docs")
from _logstats_main import first_token, unescape_js, RE_TOOLS_CALL  # noqa: E402

F = "/mnt/c/Users/hxy299/Desktop/2026/09/10/rollout-2026-09-10T21-57-56-019fa154-7ab3-7f31-b3cc-df6b6a829376_01a08b9c-626f-7731-9c4f-84b5aa56e5fa.jsonl"

raw_cmds = []
seq = []
with open(F, "r", encoding="utf-8", errors="replace") as fh:
    for line in fh:
        line = line.strip()
        if not line:
            continue
        try:
            o = json.loads(line)
        except Exception:  # noqa: BLE001
            continue
        if not isinstance(o, dict):
            continue
        pl = o.get("payload")
        if not isinstance(pl, dict):
            continue
        if o.get("type") == "event_msg" and pl.get("type") == "token_count":
            tot = (pl.get("info") or {}).get("total_token_usage") or {}
            seq.append((o.get("ordinal"), tot.get("input_tokens"), tot.get("output_tokens"), tot.get("total_tokens"), o.get("timestamp")))
        if o.get("type") == "response_item" and pl.get("type") == "custom_tool_call":
            inp = pl.get("input")
            if isinstance(inp, str) and "exec_command" in inp:
                for m in re.finditer(r"tools\.exec_command\(\s*\{", inp):
                    seg = inp[m.end() - 1: m.end() + 4000]
                    cm = re.search(r"(?:cmd|command)\s*:\s*(\"(?:[^\"\\]|\\.)*\"|'(?:[^'\\]|\\.)*'|`(?:[^`\\]|\\.)*`)", seg, re.S)
                    if cm:
                        raw_cmds.append(unescape_js(cm.group(1)[1:-1]))

print("### RAW CMD SAMPLES + first_token:")
for c in raw_cmds[:15]:
    print("   RAW :", repr(c[:180]))
    print("   TOK :", first_token(c))
    print()

print("### token_count sequence (ordinal, in, out, total, ts):")
prev = None
for r in seq:
    flag = ""
    if prev is not None and r[3] is not None and prev is not None and r[3] < prev:
        flag = "   <<<< RESET"
    print("   ", r, flag)
    if r[3] is not None:
        prev = r[3]
