#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""compact.py — one-line-per-file compact view of the stats JSON."""
import json
import sys

d = json.load(open(sys.argv[1], encoding="utf-8"))
for f in d["files"]:
    if "error" in f:
        print(f["path"], "ERROR", f["error"])
        continue
    tc = f.get("tc_last") or {}
    print(
        f"{f['path']}\n"
        f"   lines={f['lines']} bytes={f['bytes']} ts={f.get('ts_min')}..{f.get('ts_max')}\n"
        f"   tools={f['tool_names']}\n"
        f"   tools_ns={f['tools_ns_calls']}\n"
        f"   cmds={sum(f['cmd_tokens'].values())} read={f['cmd_read_like']} write={f['cmd_write_like']} other={f['cmd_other']}\n"
        f"   top_cmds={sorted(f['cmd_tokens'].items(), key=lambda x: -x[1])[:10]}\n"
        f"   patch: calls={f['apply_patch_calls']} add={f['patch_files_added']} upd={f['patch_files_updated']} del={f['patch_files_deleted']} +{f['patch_lines_added']} -{f['patch_lines_removed']}\n"
        f"   tok tc_events={f['token_count_events']} tc_last_total={tc.get('total_tokens')} tc_resets={f['tc_resets']}\n"
        f"   tur_events={f['tur_events']} tur_sum_total={f['tur_usage_sum']['total_tokens']} tur_thread_last={(f.get('tur_thread_last') or {}).get('total_tokens')}\n"
        f"   msgs={f['msg_roles']} task_started={f['task_started']} task_complete={f['task_complete']} reasoning={f['reasoning_items']}\n"
        f"   src={f.get('thread_source')} cli={f.get('cli_version')} id={f.get('session_meta_id')}"
    )
