#!/usr/bin/env python3
"""Probe: locate token / tool-call / diff structures in the rollout logs."""
import json
import os
import sys
import collections

ROOT = "/mnt/c/Users/hxy299/Desktop/2026"
BIG = os.path.join(ROOT, "07/27/rollout-2026-07-27T10-08-16-019fa154-7ab3-7f31-b3cc-df6b6a829376.jsonl")


def files():
    out = []
    for dp, _dn, fns in os.walk(ROOT):
        for fn in fns:
            if fn.endswith(".jsonl"):
                out.append(os.path.join(dp, fn))
    return sorted(out)


def walk_find_tokens(obj, path, hits, depth=0):
    """Find any key mentioning token/usage anywhere in the object."""
    if depth > 8:
        return
    if isinstance(obj, dict):
        for k, v in obj.items():
            p = f"{path}.{k}" if path else k
            lk = k.lower()
            if ("token" in lk or "usage" in lk) and not isinstance(v, (dict, list)):
                hits.setdefault(p, []).append(v)
            elif ("token" in lk or "usage" in lk) and isinstance(v, dict):
                for kk, vv in v.items():
                    if not isinstance(vv, (dict, list)):
                        hits.setdefault(f"{p}.{kk}", []).append(vv)
                walk_find_tokens(v, p, hits, depth + 1)
            else:
                walk_find_tokens(v, p, hits, depth + 1)
    elif isinstance(obj, list):
        for i, v in enumerate(obj[:5]):
            walk_find_tokens(v, f"{path}[]", hits, depth + 1)


def main():
    fs = files()
    small = [f for f in fs if os.path.getsize(f) < 2_000_000][:6]
    print("### SAMPLING SMALL FILES:", len(small))
    ev_types = collections.Counter()
    ri_types = collections.Counter()
    token_paths = collections.Counter()
    token_samples = {}
    tool_paths = collections.Counter()
    tool_samples = {}

    targets = small + [BIG]
    for path in targets:
        is_big = path == BIG
        limit = 6000 if is_big else 100000
        print(f"  scanning {path} (limit={limit})", file=sys.stderr)
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            for i, line in enumerate(fh):
                if i >= limit:
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
                t = o.get("type")
                pl = o.get("payload")
                if not isinstance(pl, dict):
                    continue
                if t == "event_msg":
                    ev_types[str(pl.get("type"))] += 1
                elif t == "response_item":
                    ri_types[str(pl.get("type"))] += 1
                # token hits
                hits = {}
                walk_find_tokens(pl, "", hits)
                for p, vals in hits.items():
                    token_paths[f"{t}.payload.{p}" if p else t] += 1
                    token_samples.setdefault(f"{t}.payload.{p}", json.dumps(vals[:6], ensure_ascii=False)[:400])
                # tool-call like
                for key in ("name", "tool_name", "recipient_name"):
                    if key in pl and not isinstance(pl[key], (dict, list)):
                        tool_paths[f"{t}.payload.{key}={pl[key]}"] += 1
                if t == "response_item" and pl.get("type") in ("function_call", "custom_tool_call", "local_shell_call", "function_call_output"):
                    tool_samples.setdefault(str(pl.get("type")), json.dumps(pl, ensure_ascii=False)[:900])

    print("\n### event_msg payload.type counts (sampled):")
    for k, v in ev_types.most_common(80):
        print(f"   {v:8d}  {k}")
    print("\n### response_item payload.type counts (sampled):")
    for k, v in ri_types.most_common(80):
        print(f"   {v:8d}  {k}")
    print("\n### token/usage key paths (sampled):")
    for k, v in token_paths.most_common(80):
        print(f"   {v:8d}  {k}")
        print(f"             sample: {token_samples.get(k,'')[:300]}")
    print("\n### name-like key values (sampled, top 60):")
    for k, v in tool_paths.most_common(60):
        print(f"   {v:8d}  {k}")
    print("\n### tool-call payload samples:")
    for k, v in tool_samples.items():
        print(f"--- {k}\n{v}\n")


if __name__ == "__main__":
    main()
