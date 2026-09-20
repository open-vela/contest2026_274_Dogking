#!/usr/bin/env python3
"""Probe 3: token_usage_record + item_completed + apply_patch argument shapes."""
import json
import os
import collections

ROOT = "/mnt/c/Users/hxy299/Desktop/2026"


def files():
    out = []
    for dp, _dn, fns in os.walk(ROOT):
        for fn in fns:
            if fn.endswith(".jsonl"):
                out.append(os.path.join(dp, fn))
    return sorted(out)


def main():
    tur_files = []
    sample_tur = []
    sample_ic = []
    patch_arg = []
    dup_session = collections.Counter()
    for p in files():
        if os.path.getsize(p) > 30_000_000:
            continue
        with open(p, "r", encoding="utf-8", errors="replace") as fh:
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
                t = o.get("type")
                pl = o.get("payload")
                if not isinstance(pl, dict):
                    continue
                if t == "session_meta":
                    dup_session[pl.get("session_id")] += 1
                if t == "token_usage_record":
                    if p not in tur_files:
                        tur_files.append(p)
                    if len(sample_tur) < 3:
                        sample_tur.append(json.dumps(o, ensure_ascii=False)[:1800])
                if t == "event_msg" and pl.get("type") == "item_completed" and len(sample_ic) < 4:
                    sample_ic.append(json.dumps(o, ensure_ascii=False)[:1200])
                if t == "response_item" and pl.get("type") == "function_call" and pl.get("name") == "apply_patch" and len(patch_arg) < 2:
                    patch_arg.append(json.dumps(pl, ensure_ascii=False)[:1800])
    print("### files containing token_usage_record:", len(tur_files))
    for f in tur_files[:10]:
        print("   ", f)
    print("\n### token_usage_record samples:")
    for s in sample_tur:
        print("   ", s, "\n")
    print("### item_completed samples:")
    for s in sample_ic:
        print("   ", s, "\n")
    print("### apply_patch function_call samples:")
    for s in patch_arg:
        print("   ", s, "\n")
    print("### distinct session_id values (top 20):")
    for k, v in dup_session.most_common(20):
        print(f"   {v:5d}  {k}")


if __name__ == "__main__":
    main()
