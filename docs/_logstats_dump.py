#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""dump_stats.py — pretty-print the JSON produced by analyze_ai_coding_logs.py"""
import json
import sys

d = json.load(open(sys.argv[1], encoding="utf-8"))
limit = int(sys.argv[2]) if len(sys.argv) > 2 else 10**9
for f in d["files"][:limit]:
    print(json.dumps(f, ensure_ascii=False, indent=1))
    print("-" * 90)
