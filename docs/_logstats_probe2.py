#!/usr/bin/env python3
"""Probe 2: token_count semantics, token_usage_record, apply_patch diffs, big-file schema."""
import json
import os
import collections
import sys

ROOT = "/mnt/c/Users/hxy299/Desktop/2026"
BIG = os.path.join(ROOT, "07/27/rollout-2026-07-27T10-08-16-019fa154-7ab3-7f31-b3cc-df6b6a829376.jsonl")
MID = os.path.join(ROOT, "08/11/rollout-2026-08-11T09-39-17-019fee79-55c2-76a0-8d31-639226a98182.jsonl")
SMALL = os.path.join(ROOT, "09/14/rollout-2026-09-14T19-00-00-01a09f92-e971-75c0-9cb7-df529fc70124.jsonl")


def scan(path, limit=None, label=""):
    print("=" * 100)
    print(f"### {label} {path}")
    ev = collections.Counter()
    ri = collections.Counter()
    names = collections.Counter()
    first_tc = None
    last_tc = None
    tc_seq = []
    tur_samples = []
    patch_samples = []
    msg_roles = collections.Counter()
    top_keys = collections.Counter()
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for i, line in enumerate(fh):
            if limit and i >= limit:
                break
            line = line.strip()
            if not line:
                continue
            try:
                o = json.loads(line)
            except Exception:  # noqa: BLE001
                continue
            if not isinstance(o, dict):
                continue
            for k in o:
                top_keys[k] += 1
            t = o.get("type")
            pl = o.get("payload")
            if not isinstance(pl, dict):
                continue
            if t == "event_msg":
                pt = str(pl.get("type"))
                ev[pt] += 1
                if pt == "token_count":
                    info = pl.get("info") or {}
                    tot = info.get("total_token_usage")
                    if first_tc is None:
                        first_tc = json.dumps(pl, ensure_ascii=False)[:1500]
                    last_tc = json.dumps(pl, ensure_ascii=False)[:1500]
                    if isinstance(tot, dict):
                        tc_seq.append((o.get("ordinal"), tot.get("input_tokens"), tot.get("output_tokens"), tot.get("total_tokens")))
                    if len(tc_seq) > 3 and len(tc_seq) < 8:
                        pass
            elif t == "response_item":
                pt = str(pl.get("type"))
                ri[pt] += 1
                if pl.get("name"):
                    names[str(pl.get("name"))] += 1
                if pt == "message":
                    msg_roles[str(pl.get("role"))] += 1
                if pt == "function_call" and pl.get("name") == "apply_patch" and len(patch_samples) < 2:
                    patch_samples.append(json.dumps(pl, ensure_ascii=False)[:2500])
                if pt == "custom_tool_call" and "apply_patch" in str(pl.get("input"))[:200] and len(patch_samples) < 4:
                    patch_samples.append(json.dumps(pl, ensure_ascii=False)[:2500])
            elif t == "token_usage_record":
                if len(tur_samples) < 3:
                    tur_samples.append(json.dumps(pl, ensure_ascii=False)[:2000])
    print("top-level keys:", dict(top_keys))
    print("event_msg types:", dict(ev))
    print("response_item types:", dict(ri))
    print("tool names:", dict(names))
    print("message roles:", dict(msg_roles))
    print("\nFIRST token_count event:", first_tc)
    print("\nLAST token_count event:", last_tc)
    print("\ntotal_token_usage sequence (ordinal, input, output, total) first 5 / last 5:", tc_seq[:5], "...", tc_seq[-5:])
    print("\ntoken_usage_record samples:")
    for s in tur_samples:
        print("   ", s)
    print("\napply_patch samples:")
    for s in patch_samples:
        print("   ", s)


def main():
    scan(SMALL, None, "SMALL")
    scan(MID, None, "MID")
    scan(BIG, 40000, "BIG(first 40k lines)")


if __name__ == "__main__":
    main()
