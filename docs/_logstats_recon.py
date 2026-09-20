#!/usr/bin/env python3
"""Recon: inspect the JSONL schema of AI coding session logs (read-only)."""
import json
import os
import sys

ROOT = "/mnt/c/Users/hxy299/Desktop/2026"


def find_files():
    out = []
    for dirpath, _dirnames, filenames in os.walk(ROOT):
        for fn in filenames:
            if fn.endswith(".jsonl"):
                p = os.path.join(dirpath, fn)
                out.append((os.path.getsize(p), p))
    out.sort()
    return out


def show(path, n=4):
    print("=" * 100)
    print("FILE:", path)
    print("SIZE:", os.path.getsize(path))
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for i, line in enumerate(fh):
            if i >= n:
                break
            line = line.strip()
            if not line:
                continue
            try:
                o = json.loads(line)
            except Exception as e:  # noqa: BLE001
                print(i, "PARSE-ERROR", e)
                continue
            if isinstance(o, dict):
                print(f"--- line {i} top-level keys: {list(o.keys())}")
                s = json.dumps(o, ensure_ascii=False)
                print("    full len:", len(s))
                print("    head:", s[:2000])
                # second-level keys
                for k, v in o.items():
                    if isinstance(v, dict):
                        print(f"      .{k} keys: {list(v.keys())}")
                    elif isinstance(v, list):
                        print(f"      .{k} list len={len(v)}")
                        for j, item in enumerate(v[:2]):
                            if isinstance(item, dict):
                                print(f"        [{j}] keys: {list(item.keys())}")
            else:
                print(f"--- line {i} type={type(o).__name__}")
    print()


def main():
    files = find_files()
    print("TOTAL FILES:", len(files))
    print("TOTAL BYTES:", sum(s for s, _ in files))
    print()
    print("SMALLEST 5:")
    for s, p in files[:5]:
        print("   ", s, p)
    print("LARGEST 3:")
    for s, p in files[-3:]:
        print("   ", s, p)
    print()

    targets = [files[0][1], files[1][1], files[len(files) // 2][1], files[-1][1]]
    for t in targets:
        show(t, n=4)

    # type-field survey across many files, first 400 lines each
    print("=" * 100)
    print("TYPE FIELD SURVEY (first 400 lines per file, sampled 12 files)")
    step = max(1, len(files) // 12)
    for s, p in files[::step][:12]:
        types = {}
        topkeys = {}
        with open(p, "r", encoding="utf-8", errors="replace") as fh:
            for i, line in enumerate(fh):
                if i >= 400:
                    break
                line = line.strip()
                if not line:
                    continue
                try:
                    o = json.loads(line)
                except Exception:  # noqa: BLE001
                    types["<parse-error>"] = types.get("<parse-error>", 0) + 1
                    continue
                if isinstance(o, dict):
                    for k in o.keys():
                        topkeys[k] = topkeys.get(k, 0) + 1
                    t = o.get("type", "<no-type>")
                    types[str(t)] = types.get(str(t), 0) + 1
        print(f"  {os.path.basename(p)}")
        print(f"     types: {types}")
    print()

    # one file: dump unique type + payload structure
    print("=" * 100)
    print("DEEP DIVE on median file:", files[len(files) // 2][1])
    seen = {}
    with open(files[len(files) // 2][1], "r", encoding="utf-8", errors="replace") as fh:
        for i, line in enumerate(fh):
            if i > 3000:
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
            t = str(o.get("type"))
            if t not in seen:
                seen[t] = json.dumps(o, ensure_ascii=False)[:1500]
    for t, sample in seen.items():
        print(f"--- type={t}")
        print("   ", sample)
        print()


if __name__ == "__main__":
    main()
