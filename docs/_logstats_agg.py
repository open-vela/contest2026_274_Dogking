#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Aggregate /tmp/logstats.json into the tables needed for the report."""
import collections
import datetime as dt
import json
import os
import sys

D = json.load(open(sys.argv[1] if len(sys.argv) > 1 else "/tmp/logstats.json", encoding="utf-8"))
F = [f for f in D["files"] if "error" not in f]
ERR = [f for f in D["files"] if "error" in f]
TOK = ("input_tokens", "cached_input_tokens", "cache_write_input_tokens",
       "output_tokens", "reasoning_output_tokens", "total_tokens")


def s(d):
    return sum(d.get(k, 0) for k in TOK) if isinstance(d, dict) else 0


def val(f, key, sub=None):
    d = f.get(key)
    if d is None:
        return 0
    if sub:
        return d.get(sub, 0)
    return d.get("total_tokens", 0)


def day_of(f):
    return "2026-" + f["path"].replace("\\", "/").split("/")[0] + "-" + f["path"].replace("\\", "/").split("/")[1]


def local(ts):
    if not ts:
        return None
    t = dt.datetime.strptime(ts[:19], "%Y-%m-%dT%H:%M:%S")
    return (t + dt.timedelta(hours=8)).strftime("%Y-%m-%d %H:%M:%S")


print("=" * 100)
print("## 1. INVENTORY")
print(f"files={len(D['files'])} ok={len(F)} errors={len(ERR)}")
print(f"total_bytes={sum(f['bytes'] for f in F):,}")
print(f"total_lines={sum(f['lines'] for f in F):,}")
print(f"parse_errors={sum(f.get('parse_errors',0) for f in F)}  blank={sum(f.get('blank_lines',0) for f in F)}")
print(f"bytes human={sum(f['bytes'] for f in F)/1024/1024:.1f} MiB")

tsmin = min(f["ts_min"] for f in F if f["ts_min"])
tsmax = max(f["ts_max"] for f in F if f["ts_max"])
print(f"ts_min_utc={tsmin}   ts_max_utc={tsmax}")
print(f"ts_min_local(+8)={local(tsmin)}   ts_max_local(+8)={local(tsmax)}")

days = collections.defaultdict(lambda: {"n": 0, "b": 0, "l": 0, "tok": 0, "asst": 0, "turns": 0, "cmds": 0, "patch": 0})
for f in F:
    d = days[day_of(f)]
    d["n"] += 1
    d["b"] += f["bytes"]
    d["l"] += f["lines"]
    d["tok"] += val(f, "tc_segment_sum", "total_tokens") or val(f, "tur_usage_sum", "total_tokens")
    d["asst"] += f.get("assistant_messages", 0)
    d["turns"] += f.get("task_started", 0)
    d["cmds"] += f.get("cmd_count", 0)
    d["patch"] += f.get("apply_patch_calls", 0)

print(f"\ndistinct_day_folders={len(days)}")
print(f"{'day':<12}{'files':>6}{'MiB':>9}{'lines':>10}{'tokens':>16}{'turns':>8}{'asst':>8}{'cmds':>8}{'patch':>7}")
for k in sorted(days):
    v = days[k]
    print(f"{k:<12}{v['n']:>6}{v['b']/1024/1024:>9.2f}{v['l']:>10,}{v['tok']:>16,}{v['turns']:>8}{v['asst']:>8}{v['cmds']:>8}{v['patch']:>7}")

utc_days = set()
for f in F:
    if f["ts_min"]:
        utc_days.add(f["ts_min"][:10])
print(f"\ndistinct_utc_days_from_timestamp={len(utc_days)} -> {sorted(utc_days)}")

print("\n" + "=" * 100)
print("## 2. SCHEMA / RECORD TYPES")
tl = collections.Counter()
for f in F:
    tl.update(f["top_level_keys"])
print("top_level_keys(first 200 lines/file):", dict(tl))
firstkeys = collections.Counter()
for f in F:
    for k in f["first_lines_keys"][:5]:
        firstkeys[tuple(k)] += 1
print("distinct first-line key tuples:", dict(firstkeys))
for key in ("type_counts", "response_item_types", "event_msg_types"):
    agg = collections.Counter()
    for f in F:
        agg.update(f[key])
    print(f"\n{key}:")
    for k, v in agg.most_common(40):
        print(f"   {v:>10,}  {k}")

print("\n" + "=" * 100)
print("## 3. TOKENS")
n_tc = [f for f in F if f.get("token_count_events")]
n_tur = [f for f in F if f.get("tur_events")]
n_both = [f for f in F if f.get("token_count_events") and f.get("tur_events")]
n_neither = [f for f in F if not f.get("token_count_events") and not f.get("tur_events")]
print(f"files_with_token_count={len(n_tc)}  files_with_token_usage_record={len(n_tur)}  both={len(n_both)}  neither={len(n_neither)}")
print("neither:", [f["path"] for f in n_neither])
if n_both:
    print("both:", [(f["path"], f["token_count_events"], f["tur_events"]) for f in n_both])

A = {k: sum(f["tc_segment_sum"].get(k, 0) for f in n_tc) for k in TOK}
B = {k: sum(f["tur_usage_sum"].get(k, 0) for f in n_tur) for k in TOK}
C = {k: sum(f["tc_last_usage_sum"].get(k, 0) for f in n_tc) for k in TOK}
print("\nMECH A  sum over files of tc_segment_sum.* (token_count / total_token_usage, segment-banked):")
for k in TOK:
    print(f"   {k:<26}{A[k]:>18,}")
print("\nMECH B  sum over files of tur_usage_sum.* (token_usage_record / usage, incremental):")
for k in TOK:
    print(f"   {k:<26}{B[k]:>18,}")
print("\nCROSSCHECK C sum of info.last_token_usage.* (per-request deltas):")
for k in TOK:
    print(f"   {k:<26}{C[k]:>18,}")

canon = 0
for f in F:
    canon += (f["tc_segment_sum"].get("total_tokens", 0) if f.get("token_count_events")
              else f["tur_usage_sum"].get("total_tokens", 0))
print(f"\nCANONICAL per-file total_tokens grand total = {canon:,}")
print(f"files using mech A = {len(n_tc)}, mech B = {len(n_tur)}")
print(f"token_segments>1 files: {[(f['path'], f['token_segments']) for f in F if f.get('token_segments',0)>1]}")

print("\nTOP 15 FILES BY TOKENS (canonical total_tokens):")
def ftok(f):
    return f["tc_segment_sum"].get("total_tokens", 0) if f.get("token_count_events") else f["tur_usage_sum"].get("total_tokens", 0)
for f in sorted(F, key=ftok, reverse=True)[:15]:
    print(f"   {ftok(f):>16,}  {f['path']}")

print("\nPER DAY token totals (canonical):")
for k in sorted(days):
    print(f"   {k}  {days[k]['tok']:>16,}")

print("\nBY thread_source:")
bysrc = collections.defaultdict(lambda: {"n": 0, "tok": 0, "asst": 0, "turns": 0, "cmds": 0, "bytes": 0, "lines": 0})
for f in F:
    v = bysrc[str(f.get("thread_source"))]
    v["n"] += 1
    v["tok"] += ftok(f)
    v["asst"] += f.get("assistant_messages", 0)
    v["turns"] += f.get("task_started", 0)
    v["cmds"] += f.get("cmd_count", 0)
    v["bytes"] += f["bytes"]
    v["lines"] += f["lines"]
for k, v in sorted(bysrc.items(), key=lambda x: -x[1]["tok"]):
    print(f"   {k:<18} files={v['n']:<4} tokens={v['tok']:>16,} turns={v['turns']:<6} asst={v['asst']:<6} cmds={v['cmds']:<6} MiB={v['bytes']/1048576:.1f}")

print("\n" + "=" * 100)
print("## 4. TOOLS / COMMANDS")
tool = collections.Counter()
ns = collections.Counter()
prog = collections.Counter()
rw = collections.Counter()
for f in F:
    tool.update(f["tool_names"])
    ns.update(f["tools_ns_calls"])
    prog.update(f["cmd_programs"])
    rw["read_like"] += f.get("cmd_read_like", 0)
    rw["write_like"] += f.get("cmd_write_like", 0)
    rw["other"] += f.get("cmd_other", 0)
print("response_item.payload.name (tool invocations):")
for k, v in tool.most_common(40):
    print(f"   {v:>10,}  {k}")
print("\ntools.<fn>( inside custom_tool_call.payload.input:")
for k, v in ns.most_common(40):
    print(f"   {v:>10,}  {k}")
print(f"\nexec_command invocations (cmd_count) total = {sum(f.get('cmd_count',0) for f in F):,}")
print("command classification (heuristic):", dict(rw))
print("\nTOP 40 shell programs invoked (from exec_command cmd / tools.exec_command cmd):")
for k, v in prog.most_common(40):
    print(f"   {v:>10,}  {k}")

print("\nPATCHES (apply_patch):")
tot = collections.Counter()
for f in F:
    for k in ("apply_patch_calls", "patch_files_added", "patch_files_updated", "patch_files_deleted",
              "patch_lines_added", "patch_lines_removed"):
        tot[k] += f.get(k, 0)
for k, v in tot.items():
    print(f"   {k:<24}{v:>12,}")
print("\nPER DAY patches / +/- lines:")
for k in sorted(days):
    a = sum(f.get("patch_lines_added", 0) for f in F if day_of(f) == k)
    r = sum(f.get("patch_lines_removed", 0) for f in F if day_of(f) == k)
    c = sum(f.get("apply_patch_calls", 0) for f in F if day_of(f) == k)
    print(f"   {k}  calls={c:<6} +{a:<8} -{r}")

print("\n" + "=" * 100)
print("## 5. VOLUME")
for k in ("assistant_messages", "user_messages", "developer_messages", "tool_messages",
          "task_started", "task_complete", "turn_aborted", "reasoning_items",
          "turn_context", "world_state", "compacted", "lines"):
    print(f"   {k:<24}{sum(f.get(k,0) for f in F):>14,}")
print("\nmsg_roles aggregate:")
mr = collections.Counter()
for f in F:
    mr.update(f["msg_roles"])
print("  ", dict(mr))
print("\nfiles with 0 assistant messages:", sum(1 for f in F if not f.get("assistant_messages")))
print("distinct session_meta ids:", len({f.get("thread_id") for f in F}))
print("distinct session_id:", collections.Counter(str(f.get("session_id")) for f in F).most_common(5))
print("originator:", collections.Counter(str(f.get("originator")) for f in F).most_common())
print("cli_version:", collections.Counter(str(f.get("cli_version")) for f in F).most_common())
print("errors:", ERR)
