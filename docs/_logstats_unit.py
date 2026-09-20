#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Unit test for command_programs / classify_cmd / split_subcommands."""
import sys
sys.path.insert(0, "/mnt/d/My-Program-Files/all-Files-Openvela/hxy299-a733-share-hub-a733-cubie-a7z-share/tools")
from analyze_ai_coding_logs import command_programs, classify_cmd, split_subcommands  # noqa: E402

CASES = [
    r"""$p='A733-A7Z-ALL-Files\contest2026_274_Dogking-dev-ai-contest-2026'; git -c safe.directory='*' -C $p status --short; git -c safe.directory='*' -C $p branch -vv""",
    r"""git -c safe.directory='*' -C 'A733-A7Z-ALL-Files\contest2026_274_Dogking-dev-ai-contest-2026' push share a733-cubie-a7z-share""",
    r"""Write-Output '--- broader handoff docs ---'; rg --files 'A733-A7Z-ALL-Files' | rg -i '(handoff|archive)$'""",
    r"""Get-Content -Raw 'A733-A7Z-ALL-Files\archives\x\docs\A733_OPENVELA_STATUS.md'; Write-Output '--- PREPARED ---'""",
    r"""wsl.exe -d Ubuntu -- bash -lc 'cd /mnt/d/My-Program/AI-Agent-Program/GPTsprogram/Openvela/quickly-openvela && source build/envsetup.sh >/dev/null && cmake --build cmake_out/luckfox-lyra-zero-w_nsh -j8'""",
    r""""C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe" -Command "cd '/mnt/d/x'; rg -n 'foo' -g '*.c'" """,
    r"""$ErrorActionPreference='Stop'; Get-ChildItem -Force | Select-Object Mode,Length,Name; Write-Output 'AGENTS'""",
    r"""cmake --build cmake_out/x -j8 2>&1 | Tee-Object -FilePath build.log""",
    r"""python3 - <<'PY'
print(1)
PY""",
    r"""npm ci && npm run build""",
    r"""sed -i 's/a/b/' file.c && make -j4""",
    r"""echo hello > out.txt""",
    r"""cat /proc/cpuinfo | head -20""",
    r"""git status --short""",
]

print("### command_programs + classify")
for c in CASES:
    print("CMD :", repr(c[:130]))
    print("  subs :", [s[:60] for s in split_subcommands(c)][:6])
    print("  progs:", command_programs(c))
    print("  class:", classify_cmd(c))
    print()
