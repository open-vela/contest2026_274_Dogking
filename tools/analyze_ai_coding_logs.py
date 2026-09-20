#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
analyze_ai_coding_logs.py
=========================
Streaming (line-by-line, bounded-memory) statistics for OpenAI Codex style
"rollout" JSONL coding-agent session logs.

READ-ONLY with respect to the input tree; the only file it creates is the
JSON result file given as the 2nd argument.

Observed record schema (every line is one JSON object)
------------------------------------------------------
    { "timestamp": "<ISO8601 Z>", "ordinal": <int>, "type": "<str>", "payload": {...} }

    type in { session_meta, response_item, event_msg, turn_context,
              world_state, token_usage_record, compacted, }

    response_item.payload.type in { message, reasoning, function_call,
              function_call_output, custom_tool_call, custom_tool_call_output,
              compaction, ... }
    event_msg.payload.type     in { task_started, task_complete, token_count,
              item_completed, thread_settings_applied, turn_aborted, ... }

Token accounting (two mutually exclusive mechanisms found in the corpus)
-----------------------------------------------------------------------
  A) type == "event_msg", payload.type == "token_count"
       payload.info.total_token_usage.{input_tokens,cached_input_tokens,
           cache_write_input_tokens,output_tokens,reasoning_output_tokens,total_tokens}
       -> CUMULATIVE per thread/rollout: monotonically non-decreasing.
          It *restarts* when a rollout is resumed (counter resets to a small
          value).  Therefore the file total is computed as the SUM OF SEGMENT
          MAXIMA (a new segment starts whenever total_tokens decreases and the
          previous segment value is banked).  Example: a file whose counter runs
          0->1,455,834 then restarts and runs 0->13,415,406 totals 14,871,240.
       payload.info.last_token_usage.* -> per-request delta (reported, not summed
          across files because it is not additive per file).

  B) type == "token_usage_record"
       payload.usage.{...}            -> per-response INCREMENTAL  -> summable
       payload.turn_token_usage.{...} -> cumulative within a turn
       payload.thread_token_usage.{...} -> cumulative within a thread -> last/max

Usage
-----
    python3 analyze_ai_coding_logs.py <ROOT_DIR> <OUT.json> [--workers N]

Progress is printed to stderr.  Files larger than 50 MB stream extra
"[progress]" lines every 200k records.
"""
import argparse
import collections
import json
import os
import re
import sys
import time
from concurrent.futures import ProcessPoolExecutor, as_completed

TOKEN_KEYS = (
    "input_tokens",
    "cached_input_tokens",
    "cache_write_input_tokens",
    "output_tokens",
    "reasoning_output_tokens",
    "total_tokens",
)

RE_TOOLS_CALL = re.compile(r"tools\.([A-Za-z_][A-Za-z0-9_]*)\s*\(")
RE_PATCH_BLOCK = re.compile(r"\*\*\* Begin Patch(.*?)\*\*\* End Patch", re.S)
RE_PATCH_OP = re.compile(r"^\*\*\* (Add|Update|Delete) File: (.+)$", re.M)
RE_ADD_LINE = re.compile(r"^\+(?!\+\+)", re.M)
RE_DEL_LINE = re.compile(r"^-(?!--)", re.M)
# cmd: "..." | '...' | `...`  inside a JS tools.exec_command({...}) object literal
RE_JS_CMD = re.compile(
    r"(?:^|[{,\s])(?:cmd|command)\s*:\s*"
    r"(\"(?:[^\"\\]|\\.)*\"|'(?:[^'\\]|\\.)*'|`(?:[^`\\]|\\.)*`)",
    re.S,
)

WORD_RE = lambda *words: re.compile(r"(?<![\w-])(?:" + "|".join(words) + r")(?![\w-])", re.I)
# state-mutating verbs (heuristics, documented in the report)
RE_WRITE = WORD_RE(
    r"Set-Content", r"Add-Content", r"Out-File", r"New-Item", r"Remove-Item",
    r"Move-Item", r"Copy-Item", r"Rename-Item", r"Clear-Content", r"mkdir",
    r"rmdir", r"touch", r"tee", r"chmod", r"chown", r"cp", r"mv", r"rm", r"del",
    r"unzip", r"tar", r"dd", r"mkfs", r"ln", r"apply_patch", r"make", r"cmake",
    r"ninja", r"npm", r"pnpm", r"pip", r"apt", r"apt-get", r"systemctl",
)
RE_SED_I = re.compile(r"(?<![\w-])sed\b[^;|&]*?\s-i", re.I)
RE_WRITE_GIT = WORD_RE(r"add", r"commit", r"push", r"apply", r"checkout", r"reset",
                       r"merge", r"rebase", r"tag", r"init", r"stash", r"clean",
                       r"rm", r"mv", r"restore", r"pull", r"fetch", r"clone")
RE_REDIRECT = re.compile(r"(?<![=<>!\d])>>?(?!>)")
RE_READ = WORD_RE(
    r"Get-Content", r"Get-ChildItem", r"Get-Item", r"Select-String", r"Test-Path",
    r"Resolve-Path", r"Get-FileHash", r"cat", r"head", r"tail", r"less", r"more",
    r"grep", r"rg", r"find", r"ls", r"dir", r"wc", r"stat", r"tree",
    r"md5sum", r"sha256sum", r"readlink", r"realpath",
)
RE_GIT = WORD_RE(r"git")

SKIP_PROGRAMS = {
    "cd", "set", "export", "pushd", "popd", "if", "then", "else", "elseif", "fi",
    "do", "done", "foreach", "for", "while", "function", "param", "return",
    "exit", "try", "catch", "finally", "begin", "process", "end", "{}", "(",
    ")", "$erroractionpreference", "$null", "true", "false", "echo-off",
}


# --------------------------------------------------------------------------- #
# helpers
# --------------------------------------------------------------------------- #
def blank_tokens():
    return {k: 0 for k in TOKEN_KEYS}


def add_tokens(acc, d):
    if not isinstance(d, dict):
        return
    for k in TOKEN_KEYS:
        v = d.get(k)
        if isinstance(v, (int, float)) and not isinstance(v, bool):
            acc[k] += int(v)


def max_tokens(cur, d):
    if not isinstance(d, dict):
        return cur
    if cur is None:
        cur = blank_tokens()
    for k in TOKEN_KEYS:
        v = d.get(k)
        if isinstance(v, (int, float)) and not isinstance(v, bool):
            if int(v) > cur[k]:
                cur[k] = int(v)
    return cur


def unescape_js(s):
    """Turn an escaped JS string body into plain text (used for patch extraction)."""
    return (s.replace("\\r\\n", "\n").replace("\\n", "\n").replace("\\t", "\t")
             .replace('\\"', '"').replace("\\'", "'").replace("\\\\", "\\"))


def split_subcommands(cmd):
    """Quote-aware split of a shell command string on ; && || | and newlines."""
    out, cur = [], []
    quote = None
    i, n = 0, len(cmd)
    while i < n:
        ch = cmd[i]
        if quote:
            cur.append(ch)
            if ch == "\\" and i + 1 < n and quote != "'":
                cur.append(cmd[i + 1])
                i += 2
                continue
            if ch == quote:
                quote = None
            i += 1
            continue
        if ch in "'\"`":
            quote = ch
            cur.append(ch)
            i += 1
            continue
        if ch in ";\n":
            out.append("".join(cur)); cur = []
            i += 1
            continue
        if ch == "&":
            if cur and cur[-1] == ">":          # keep redirections such as 2>&1 intact
                cur.append(ch)
                i += 1
                continue
            out.append("".join(cur)); cur = []
            i += 2 if (i + 1 < n and cmd[i + 1] == "&") else 1
            continue
        if ch == "|":
            out.append("".join(cur)); cur = []
            i += 2 if (i + 1 < n and cmd[i + 1] == "|") else 1
            continue
        cur.append(ch)
        i += 1
    out.append("".join(cur))
    return [s.strip() for s in out if s.strip()]


def _strip_quotes(tok):
    return tok.strip().strip("\"'`")


def unwrap_launchers(s):
    """Remove launcher prefixes (powershell -Command, wsl -- bash -lc, ...)."""
    s = s.strip()
    for _ in range(8):
        before = s
        s = re.sub(r"^[&.]\s*", "", s).strip()
        s = re.sub(r"^(?:sudo|nohup|time|env)\s+", "", s, flags=re.I).strip()
        # VAR=value / $var=value prefixes
        s = re.sub(r'^["\']?[\w$]+=(?:"[^"]*"|\'[^\']*\'|\S*)\s*', "", s).strip()
        m = re.match(r'^(?:"[^"]*[\\/])?(?:powershell|pwsh)(?:\.exe)?"?\s*(.*)$', s, re.I | re.S)
        if m:
            rest = m.group(1)
            m2 = re.search(r"(?:^|\s)-c(?:ommand)?\s+(.*)$", rest, re.I | re.S)
            s = m2.group(1) if m2 else rest
            continue
        m = re.match(r'^(?:"[^"]*[\\/])?wsl(?:\.exe)?"?\s*(.*)$', s, re.I | re.S)
        if m:
            rest = m.group(1)
            rest = re.sub(r"^\s*(?:-d|--distribution)\s+\S+\s*", "", rest, flags=re.I)
            rest = re.sub(r"^\s*(?:-u|--user)\s+\S+\s*", "", rest, flags=re.I)
            rest = re.sub(r"^\s*(?:--cd|--exec)\s+\S*\s*", "", rest, flags=re.I)
            rest = re.sub(r"^\s*--\s*", "", rest)
            s = rest.strip()
            continue
        m = re.match(r'^(?:"[^"]*[\\/])?(?:bash|zsh|dash|sh)(?:\.exe)?"?\s*(.*)$', s, re.I | re.S)
        if m:
            rest = m.group(1)
            m2 = re.match(r"^\s*-\w*c\w*\s+(.*)$", rest, re.S)
            s = m2.group(1) if m2 else rest
            continue
        m = re.match(r'^(?:"[^"]*[\\/])?cmd(?:\.exe)?"?\s+(.*)$', s, re.I | re.S)
        if m:
            s = re.sub(r"^\s*/c\s+", "", m.group(1), flags=re.I)
            continue
        if s == before:
            break
    return s.strip()


def strip_surrounding_quotes(s):
    s = s.strip()
    if len(s) >= 2 and s[0] == s[-1] and s[0] in "'\"`":
        return s[1:-1].strip()
    return s


def command_programs(cmd, depth=0):
    """Best-effort list of programs invoked by one shell command string."""
    progs = []
    for sub in split_subcommands(cmd):
        s = unwrap_launchers(sub)
        inner = strip_surrounding_quotes(s)
        # a launcher was unwrapped and what remains is itself a command line:
        # re-parse it so that `bash -lc 'cd x && cmake --build y'` yields cmake.
        if depth < 4 and inner != sub and split_subcommands(inner):
            progs.extend(command_programs(inner, depth + 1))
            continue
        tok = re.split(r"\s+", s, maxsplit=1)[0]
        tok = _strip_quotes(tok)
        if not tok or "=" in tok:
            continue
        base = os.path.basename(tok.replace("\\", "/"))
        if re.search(r"\.(exe|cmd|bat|ps1|sh|py|js)$", base, re.I):
            tok = base
        tok = tok.lower().strip("&.")
        if not tok or tok in SKIP_PROGRAMS or tok.startswith("$"):
            continue
        if any(ch in tok for ch in "(){}#"):
            continue
        progs.append(tok[:60])
    return progs


def classify_cmd(cmd):
    """Coarse heuristic: does this shell command mutate state? (documented)"""
    c = cmd
    if RE_SED_I.search(c) or RE_REDIRECT.search(c) or RE_WRITE.search(c):
        return "write_like"
    if RE_GIT.search(c):
        # 'git status/log/diff/show' are read-only; anything else is treated as mutating
        return "write_like" if RE_WRITE_GIT.search(c) else "read_like"
    if RE_READ.search(c):
        return "read_like"
    return "other"


# --------------------------------------------------------------------------- #
# per-file analysis
# --------------------------------------------------------------------------- #
def analyze_file(args):
    path, rel, size = args
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
        "tools_ns_calls": collections.Counter(),
        "cmd_programs": collections.Counter(),
        "cmd_count": 0,
        "cmd_read_like": 0,
        "cmd_write_like": 0,
        "cmd_other": 0,
        "msg_roles": collections.Counter(),
        "turn_context": 0,
        "world_state": 0,
        "compacted": 0,
        "reasoning_items": 0,
        "model_provider": None,
        "cli_version": None,
        "originator": None,
        "thread_source": None,
        "source": None,
        "cwd": None,
        "thread_id": None,
        "session_id": None,
        # tokens
        "token_count_events": 0,
        "token_segments": 0,
        "tc_segment_sum": blank_tokens(),
        "tc_first": None,
        "tc_last": None,
        "tc_grand_max": None,
        "tc_last_usage": None,
        "tc_last_usage_sum": blank_tokens(),
        "tur_events": 0,
        "tur_usage_sum": blank_tokens(),
        "tur_thread_last": None,
        # patches
        "apply_patch_calls": 0,
        "patch_files_added": 0,
        "patch_files_updated": 0,
        "patch_files_deleted": 0,
        "patch_lines_added": 0,
        "patch_lines_removed": 0,
        # turns / messages
        "task_started": 0,
        "task_complete": 0,
        "turn_aborted": 0,
        "assistant_messages": 0,
        "user_messages": 0,
        "developer_messages": 0,
        "tool_messages": 0,
        "assistant_reasoning_items": 0,
    }

    run_tok = None          # cumulative counters of the CURRENT segment
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            st["lines"] += 1
            if size > 50_000_000 and st["lines"] % 200000 == 0:
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
            if st["lines"] <= 200:
                for k in o.keys():
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
                st["model_provider"] = pl.get("model_provider")
                st["cli_version"] = pl.get("cli_version")
                st["originator"] = pl.get("originator")
                st["thread_source"] = pl.get("thread_source")
                src = pl.get("source")
                if isinstance(src, dict):
                    st["source"] = json.dumps(src, ensure_ascii=False)[:120]
                elif isinstance(src, str):
                    st["source"] = src
                st["cwd"] = pl.get("cwd")
                st["thread_id"] = pl.get("id")
                st["session_id"] = pl.get("session_id")
                continue
            if t in ("turn_context", "world_state", "compacted"):
                st[t] += 1
                continue
            if t == "token_usage_record" and isinstance(pl, dict):
                st["tur_events"] += 1
                add_tokens(st["tur_usage_sum"], pl.get("usage"))
                st["tur_thread_last"] = max_tokens(st["tur_thread_last"], pl.get("thread_token_usage"))
                continue
            if not isinstance(pl, dict):
                continue

            if t == "event_msg":
                pt = str(pl.get("type"))
                st["event_msg_types"][pt] += 1
                if pt == "token_count":
                    st["token_count_events"] += 1
                    info = pl.get("info") if isinstance(pl.get("info"), dict) else {}
                    tot = info.get("total_token_usage")
                    if isinstance(tot, dict):
                        cur = {k: tot.get(k) for k in TOKEN_KEYS}
                        if st["tc_first"] is None:
                            st["tc_first"] = cur
                        st["tc_last"] = cur
                        st["tc_grand_max"] = max_tokens(st["tc_grand_max"], tot)
                        tt = tot.get("total_tokens")
                        tt = int(tt) if isinstance(tt, (int, float)) and not isinstance(tt, bool) else None
                        if run_tok is None:
                            run_tok = {k: (int(v) if isinstance(v, (int, float)) else 0) for k, v in cur.items()}
                        elif tt is not None and isinstance(run_tok.get("total_tokens"), int) and tt < run_tok["total_tokens"]:
                            add_tokens(st["tc_segment_sum"], run_tok)   # bank finished segment
                            st["token_segments"] += 1
                            run_tok = {k: (int(v) if isinstance(v, (int, float)) else 0) for k, v in cur.items()}
                        else:
                            for k in TOKEN_KEYS:
                                v = cur.get(k)
                                if isinstance(v, (int, float)) and not isinstance(v, bool):
                                    run_tok[k] = max(run_tok.get(k, 0), int(v))
                    lastu = info.get("last_token_usage")
                    if isinstance(lastu, dict):
                        st["tc_last_usage"] = {k: lastu.get(k) for k in TOKEN_KEYS}
                        add_tokens(st["tc_last_usage_sum"], lastu)
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
                    elif role == "tool":
                        st["tool_messages"] += 1
                elif pt == "reasoning":
                    st["reasoning_items"] += 1
                elif pt in ("function_call", "custom_tool_call"):
                    nm = str(pl.get("name"))
                    st["tool_names"][nm] += 1
                    patch_texts = []
                    if pt == "function_call":
                        args = pl.get("arguments")
                        if isinstance(args, str):
                            try:
                                a = json.loads(args)
                            except Exception:
                                a = None
                            if isinstance(a, dict):
                                if isinstance(a.get("cmd"), str):
                                    c = a["cmd"]
                                    st["cmd_count"] += 1
                                    st["cmd_" + classify_cmd(c)] += 1
                                    for p in command_programs(c):
                                        st["cmd_programs"][p] += 1
                                if nm == "apply_patch" and isinstance(a.get("patch"), str):
                                    st["apply_patch_calls"] += 1
                                    patch_texts.append(a["patch"])
                    else:  # custom_tool_call -> JavaScript source in .input
                        inp = pl.get("input")
                        if isinstance(inp, str):
                            for m in RE_TOOLS_CALL.finditer(inp):
                                st["tools_ns_calls"][m.group(1)] += 1
                            if "exec_command" in inp:
                                for m in re.finditer(r"tools\.exec_command\s*\(\s*\{", inp):
                                    seg = inp[m.end() - 1: m.end() + 8000]
                                    cm = RE_JS_CMD.search(seg)
                                    if cm:
                                        c = unescape_js(cm.group(1)[1:-1])
                                        st["cmd_count"] += 1
                                        st["cmd_" + classify_cmd(c)] += 1
                                        for p in command_programs(c):
                                            st["cmd_programs"][p] += 1
                            if "apply_patch" in inp:
                                st["apply_patch_calls"] += len(re.findall(r"tools\.apply_patch\s*\(", inp))
                                patch_texts.append(unescape_js(inp))
                    for ptxt in patch_texts:
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

    if run_tok is not None:
        add_tokens(st["tc_segment_sum"], run_tok)
        st["token_segments"] += 1

    for k in ("type_counts", "response_item_types", "event_msg_types", "top_level_keys",
              "tool_names", "tools_ns_calls", "cmd_programs", "msg_roles"):
        st[k] = dict(st[k])
    return st


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root")
    ap.add_argument("out")
    ap.add_argument("--workers", type=int, default=4)
    a = ap.parse_args()

    files = []
    for dp, _dn, fns in os.walk(a.root):
        for fn in fns:
            if fn.endswith(".jsonl"):
                p = os.path.join(dp, fn)
                files.append((p, os.path.relpath(p, a.root), os.path.getsize(p)))
    files.sort(key=lambda x: x[1])
    total_bytes = sum(f[2] for f in files)
    print(f"[info] {len(files)} .jsonl files, {total_bytes:,} bytes under {a.root}",
          file=sys.stderr, flush=True)

    results = []
    t0 = time.time()
    jobs = [(p, rel, sz) for p, rel, sz in files]
    if a.workers > 1 and len(jobs) > 4:
        with ProcessPoolExecutor(max_workers=a.workers) as ex:
            futs = {}
            for j in jobs:
                futs[ex.submit(analyze_file, j)] = j
            done = 0
            for fu in as_completed(futs):
                j = futs[fu]
                done += 1
                try:
                    st = fu.result()
                except Exception as e:  # noqa: BLE001
                    st = {"path": j[1], "bytes": j[2], "error": repr(e)}
                print(f"[{done}/{len(jobs)}] {j[1]} ({j[2]:,} B) t+{time.time()-t0:.0f}s",
                      file=sys.stderr, flush=True)
                results.append(st)
        results.sort(key=lambda s: s["path"])
    else:
        for i, j in enumerate(jobs, 1):
            print(f"[{i}/{len(jobs)}] {j[1]} ({j[2]:,} B) t+{time.time()-t0:.0f}s",
                  file=sys.stderr, flush=True)
            try:
                results.append(analyze_file(j))
            except Exception as e:  # noqa: BLE001
                results.append({"path": j[1], "bytes": j[2], "error": repr(e)})

    with open(a.out, "w", encoding="utf-8") as fh:
        json.dump({"root": a.root, "generated": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                   "files": results}, fh, ensure_ascii=False, indent=1)
    print(f"[done] wrote {a.out} in {time.time()-t0:.0f}s", file=sys.stderr, flush=True)


if __name__ == "__main__":
    main()
