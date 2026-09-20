#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
analyze_ai_coding_logs.py — streaming (line-by-line, bounded-memory) statistics for
OpenAI Codex style rollout JSONL session logs.

READ-ONLY with respect to the input tree. Writes only its JSON result file.

Schema observed (top level of every record):
    { "timestamp": ISO8601 str, "ordinal": int, "type": str, "payload": {...} }
    type in {session_meta, response_item, event_msg, turn_context, world_state,
             token_usage_record, compacted}

Token accounting — two mutually-exclusive mechanisms found:
  A) event_msg / payload.type == "token_count"
       payload.info.total_token_usage.{input_tokens,cached_input_tokens,
         cache_write_input_tokens,output_tokens,reasoning_output_tokens,total_tokens}
       -> CUMULATIVE per thread (monotonic). Session total = LAST/Max value.
  B) type == "token_usage_record"
       payload.usage.{...}                 -> per-response INCREMENTAL -> summable
       payload.turn_token_usage.{...}      -> cumulative within turn
       payload.thread_token_usage.{...}    -> cumulative within thread -> take last
Usage:
    python3 analyze_ai_coding_logs.py <ROOT> <OUT.json>
"""
import collections
import json
import os
import re
import sys
import time

TOKEN_KEYS = (
    "input_tokens",
    "cached_input_tokens",
    "cache_write_input_tokens",
    "output_tokens",
    "reasoning_output_tokens",
    "total_tokens",
)

# ---------------------------------------------------------------- regexes
RE_TOOLS_CALL = re.compile(r"tools\.([A-Za-z_][A-Za-z0-9_]*)\s*\(")
RE_PATCH_BLOCK = re.compile(r"\*\*\* Begin Patch(.*?)\*\*\* End Patch", re.S)
RE_PATCH_OP = re.compile(r"^\*\*\* (Add|Update|Delete) File: (.+)$", re.M)
RE_ADD_LINE = re.compile(r"^\+(?!\+\+)", re.M)
RE_DEL_LINE = re.compile(r"^-(?!--)", re.M)

# heuristics for classifying a shell command line (documented in the report)
READ_VERBS = (
    "get-content", "get-childitem", "select-string", "cat ", "cat\t", "head ", "tail ",
    "less ", "sed -n", "grep ", "rg ", "find ", "ls ", "ls\t", "dir ", "type ",
    "wc -l", "stat ", "file ", "tree ", "realpath", "readlink", "du ", "md5sum",
    "sha256sum", "git status", "git log", "git diff", "git show", "git branch",
    "test-path", "resolve-path", "where.exe", "which ",
)
WRITE_VERBS = (
    "set-content", "add-content", "out-file", "new-item", "mkdir", "rm ", "del ",
    "remove-item", "copy-item", "move-item", "cp ", "mv ", "rm -rf", "touch ",
    "tee ", ">", ">>", "sed -i", "chmod ", "git apply", "git commit", "git add",
    "unzip", "tar ", "dd ", "mkfs", "ln -s",
)


def classify_cmd(cmd):
    """Very coarse read/write classification of a shell command string."""
    c = cmd.lower()
    if any(v in c for v in WRITE_VERBS):
        return "write_like"
    if any(v in c for v in READ_VERBS):
        return "read_like"
    return "other"


def first_token(cmd):
    """Best-effort extraction of the 'program' that a command invokes."""
    c = cmd.strip()
    if not c:
        return "<empty>"
    # unwrap common launchers so we see the real program
    m = re.match(r"^(?:\S*powershell(?:\.exe)?|\S*pwsh(?:\.exe)?)\b.*?-c(?:ommand)?\s+(.*)$", c, re.S | re.I)
    if m:
        c = m.group(1).strip()
    m = re.match(r"^(?:\S*wsl(?:\.exe)?)\b(?:\s+-d\s+\S+)?\s*(?:--)?\s*(.*)$", c, re.S | re.I)
    if m and m.group(1).strip():
        c = m.group(1).strip()
    m = re.match(r"^(?:\S*bash|\S*sh)\b\s+-l?c\s+(.*)$", c, re.S | re.I)
    if m:
        c = m.group(1).strip()
    c = c.strip("\"'` ")
    # strip env assignments like FOO=bar cmd
    c = re.sub(r"^(?:[A-Za-z_][A-Za-z0-9_]*=[^\s]+\s+)+", "", c)
    c = re.sub(r"^[&;|\s]+", "", c)
    c = re.sub(r"^[.\u2026]+[\\/]", "", c)
    tok = re.split(r"[\s;|&]+", c, maxsplit=1)[0] if c else "<empty>"
    tok = os.path.basename(tok.replace("\\", "/")) if tok else "<empty>"
    return tok.lower()[:60] or "<empty>"


def unescape_js(s):
    """Turn the escaped-JS-string body into readable text (for patch extraction)."""
    s = s.replace("\\r\\n", "\n").replace("\\n", "\n").replace("\\t", "\t")
    s = s.replace('\\"', '"').replace("\\'", "'").replace("\\\\", "\\")
    return s


def blank_token_acc():
    return {k: 0 for k in TOKEN_KEYS}


def add_tokens(acc, d):
    if not isinstance(d, dict):
        return False
    got = False
    for k in TOKEN_KEYS:
        v = d.get(k)
        if isinstance(v, bool):
            continue
        if isinstance(v, (int, float)):
            acc[k] += int(v)
            got = True
    return got


def max_tokens(cur, d):
    """Keep the element-wise maximum (cumulative counters are monotonic)."""
    if not isinstance(d, dict):
        return cur
    if cur is None:
        cur = blank_token_acc()
    for k in TOKEN_KEYS:
        v = d.get(k)
        if isinstance(v, (int, float)) and not isinstance(v, bool):
            if int(v) > cur[k]:
                cur[k] = int(v)
    return cur


def analyze_file(path, rel, size, progress):
    st = {
        "path": rel,
        "bytes": size,
        "lines": 0,
        "parse_errors": 0,
        "blank_lines": 0,
        "ts_min": None,
        "ts_max": None,
        "type_counts": collections.Counter(),
        "response_item_types": collections.Counter(),
        "event_msg_types": collections.Counter(),
        "top_level_keys": collections.Counter(),
        "first_lines_keys": [],
        "tool_names": collections.Counter(),
        "tools_ns_calls": collections.Counter(),   # tools.<x>( inside custom_tool_call input
        "cmd_tokens": collections.Counter(),       # frequency of invoked program
        "cmd_read_like": 0,
        "cmd_write_like": 0,
        "cmd_other": 0,
        "msg_roles": collections.Counter(),
        "turn_context": 0,
        "world_state": 0,
        "compacted": 0,
        "reasoning_items": 0,
        "model": None,
        "cli_version": None,
        "originator": None,
        "thread_source": None,
        "cwd": None,
        "session_meta_id": None,
        "session_id": None,
        # tokens
        "token_count_events": 0,
        "tc_first": None,
        "tc_last": None,
        "tc_max": None,
        "tc_resets": 0,
        "tur_events": 0,
        "tur_usage_sum": blank_token_acc(),
        "tur_thread_last": None,
        "tur_turn_last": None,
        # patches
        "apply_patch_calls": 0,
        "patch_files_added": 0,
        "patch_files_updated": 0,
        "patch_files_deleted": 0,
        "patch_lines_added": 0,
        "patch_lines_removed": 0,
        "patch_bytes": 0,
        # tasks
        "task_started": 0,
        "task_complete": 0,
        "turn_aborted": 0,
        "assistant_messages": 0,
        "user_messages": 0,
        "developer_messages": 0,
    }
    prev_total = None
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            st["lines"] += 1
            if progress and st["lines"] % 200000 == 0:
                print(f"    [progress] {rel} line {st['lines']:,}", file=sys.stderr, flush=True)
            line = line.strip()
            if not line:
                st["blank_lines"] += 1
                continue
            try:
                o = json.loads(line)
            except Exception:
                st["parse_errors"] += 1
                continue
            if not isinstance(o, dict):
                st["parse_errors"] += 1
                continue
            if st["lines"] <= 5:
                st["first_lines_keys"].append(list(o.keys()))
            for k in o.keys():
                if st["lines"] <= 200:
                    st["top_level_keys"][k] += 1

            ts = o.get("timestamp")
            if isinstance(ts, str):
                if st["ts_min"] is None or ts < st["ts_min"]:
                    st["ts_min"] = ts
                if st["ts_max"] is None or ts > st["ts_max"]:
                    st["ts_max"] = ts

            t = o.get("type")
            st["type_counts"][str(t)] += 1
            pl = o.get("payload")

            if t == "session_meta" and isinstance(pl, dict):
                st["model"] = (pl.get("model_provider") or st["model"])
                st["cli_version"] = pl.get("cli_version")
                st["originator"] = pl.get("originator")
                st["thread_source"] = pl.get("thread_source")
                st["cwd"] = pl.get("cwd")
                st["session_meta_id"] = pl.get("id")
                st["session_id"] = pl.get("session_id")
                continue
            if t == "turn_context":
                st["turn_context"] += 1
                continue
            if t == "world_state":
                st["world_state"] += 1
                continue
            if t == "compacted":
                st["compacted"] += 1
                continue

            if t == "token_usage_record" and isinstance(pl, dict):
                st["tur_events"] += 1
                add_tokens(st["tur_usage_sum"], pl.get("usage"))
                st["tur_thread_last"] = max_tokens(st["tur_thread_last"], pl.get("thread_token_usage"))
                st["tur_turn_last"] = max_tokens(st["tur_turn_last"], pl.get("turn_token_usage"))
                continue

            if not isinstance(pl, dict):
                continue

            if t == "event_msg":
                pt = str(pl.get("type"))
                st["event_msg_types"][pt] += 1
                if pt == "token_count":
                    st["token_count_events"] += 1
                    info = pl.get("info") or {}
                    tot = info.get("total_token_usage")
                    if isinstance(tot, dict):
                        if st["tc_first"] is None:
                            st["tc_first"] = {k: tot.get(k) for k in TOKEN_KEYS}
                        st["tc_last"] = {k: tot.get(k) for k in TOKEN_KEYS}
                        tt = tot.get("total_tokens")
                        if isinstance(tt, (int, float)):
                            if prev_total is not None and tt < prev_total:
                                st["tc_resets"] += 1
                            prev_total = tt
                        st["tc_max"] = max_tokens(st["tc_max"], tot)
                elif pt == "task_started":
                    st["task_started"] += 1
                elif pt == "task_complete":
                    st["task_complete"] += 1
                elif pt == "turn_aborted":
                    st["turn_aborted"] += 1
                continue

            if t == "response_item":
                pt = str(pl.get("type"))
                st["response_item_types"][pt] += 1
                if pt == "message":
                    role = str(pl.get("role"))
                    st["msg_roles"][role] += 1
                    if role == "assistant":
                        st["assistant_messages"] += 1
                    elif role == "user":
                        st["user_messages"] += 1
                    elif role == "developer":
                        st["developer_messages"] += 1
                elif pt == "reasoning":
                    st["reasoning_items"] += 1
                elif pt in ("function_call", "custom_tool_call"):
                    nm = str(pl.get("name"))
                    st["tool_names"][nm] += 1
                    patch_texts = []
                    if pt == "function_call":
                        args = pl.get("arguments")
                        if isinstance(args, str):
                            if nm in ("exec_command", "write_stdin", "exec"):
                                try:
                                    a = json.loads(args)
                                except Exception:
                                    a = None
                                if isinstance(a, dict) and isinstance(a.get("cmd"), str):
                                    cmd = a["cmd"]
                                    st["cmd_tokens"][first_token(cmd)] += 1
                                    cls = classify_cmd(cmd)
                                    st["cmd_" + cls] += 1
                            if nm == "apply_patch":
                                st["apply_patch_calls"] += 1
                                try:
                                    a = json.loads(args)
                                except Exception:
                                    a = None
                                if isinstance(a, dict) and isinstance(a.get("patch"), str):
                                    patch_texts.append(a["patch"])
                        elif isinstance(args, dict):
                            if nm == "apply_patch" and isinstance(args.get("patch"), str):
                                st["apply_patch_calls"] += 1
                                patch_texts.append(args["patch"])
                    else:  # custom_tool_call -> JS source in "input"
                        inp = pl.get("input")
                        if isinstance(inp, str):
                            for m in RE_TOOLS_CALL.finditer(inp):
                                inner = m.group(1)
                                st["tools_ns_calls"][inner] += 1
                            if "exec_command" in inp:
                                for m in re.finditer(r"tools\.exec_command\(\s*\{", inp):
                                    seg = inp[m.end() - 1: m.end() + 4000]
                                    cm = re.search(r"(?:cmd|command)\s*:\s*(\"(?:[^\"\\]|\\.)*\"|'(?:[^'\\]|\\.)*'|`(?:[^`\\]|\\.)*`)", seg, re.S)
                                    if cm:
                                        raw = cm.group(1)[1:-1]
                                        cmd = unescape_js(raw)
                                        st["cmd_tokens"][first_token(cmd)] += 1
                                        cls = classify_cmd(cmd)
                                        st["cmd_" + cls] += 1
                            if "apply_patch" in inp:
                                st["apply_patch_calls"] += len(re.findall(r"tools\.apply_patch\s*\(", inp))
                                patch_texts.append(unescape_js(inp))
                    for ptxt in patch_texts:
                        st["patch_bytes"] += len(ptxt)
                        for block in RE_PATCH_BLOCK.findall(ptxt):
                            for op, _fn in RE_PATCH_OP.findall(block):
                                if op == "Add":
                                    st["patch_files_added"] += 1
                                elif op == "Update":
                                    st["patch_files_updated"] += 1
                                else:
                                    st["patch_files_deleted"] += 1
                            st["patch_lines_added"] += len(RE_ADD_LINE.findall(block))
                            st["patch_lines_removed"] += len(RE_DEL_LINE.findall(block))
                continue

    for k in ("type_counts", "response_item_types", "event_msg_types", "top_level_keys",
              "tool_names", "tools_ns_calls", "cmd_tokens", "msg_roles"):
        st[k] = dict(st[k])
    return st


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "/mnt/c/Users/hxy299/Desktop/2026"
    out = sys.argv[2] if len(sys.argv) > 2 else "/tmp/logstats.json"

    files = []
    for dp, _dn, fns in os.walk(root):
        for fn in fns:
            if fn.endswith(".jsonl"):
                p = os.path.join(dp, fn)
                files.append((os.path.getsize(p), p))
    files.sort(key=lambda x: x[1])
    print(f"[info] found {len(files)} .jsonl files, {sum(s for s, _ in files):,} bytes", file=sys.stderr, flush=True)

    results = []
    t0 = time.time()
    for i, (size, p) in enumerate(files, 1):
        rel = os.path.relpath(p, root)
        print(f"[{i}/{len(files)}] {rel}  ({size:,} bytes)  t+{time.time()-t0:.0f}s", file=sys.stderr, flush=True)
        try:
            st = analyze_file(p, rel, size, progress=size > 50_000_000)
        except Exception as e:  # noqa: BLE001
            print(f"    !! ERROR {e}", file=sys.stderr, flush=True)
            st = {"path": rel, "bytes": size, "error": repr(e)}
        results.append(st)

    with open(out, "w", encoding="utf-8") as fh:
        json.dump({"root": root, "files": results}, fh, ensure_ascii=False, indent=1)
    print(f"[done] wrote {out} in {time.time()-t0:.0f}s", file=sys.stderr, flush=True)


if __name__ == "__main__":
    main()
