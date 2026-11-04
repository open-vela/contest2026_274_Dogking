#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile unchanged production UFS M-PHY/UIC core; no MMIO/devices/network."""
import os
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

repo = Path(__file__).resolve().parent.parent
chip = repo/'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733'
with tempfile.TemporaryDirectory(prefix='a733-ufs-link-') as folder:
    exe = Path(folder)/'test'
    subprocess.run(['gcc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
        '-fno-pie', '-no-pie', '-fsanitize=address,undefined', '-I'+str(chip),
        str(chip/'a733_ufs_link.c'), str(repo/'tools/test-a733-ufs-link.c'),
        '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=60,
        env={**os.environ, 'ASAN_OPTIONS':'detect_leaks=1'})
    # Dry-run the critical-source checker against a NEW fake SDK. The real
    # SDK is never staged/edited by this test. A full CMake build is separate.
    fixture = Path(folder)/'sdk'
    shutil.copytree(repo/'board/a733-cubie-a7z/openvela-overlay', fixture)
    checker = repo/'tools/check-a733-tensor-staged.py'
    checked = subprocess.run([sys.executable, str(checker), str(fixture), '--ufs'],
        capture_output=True, check=True, text=True)
    assert len(json.loads(checked.stdout)['files']) == 57
    script = (repo/'tools/build-a733-wapi.sh').read_text()
    files = re.search(r'^files=\(\n(.*?)^\)', script, re.M | re.S).group(1).split()
    for name in ['a733_ufs_link.c', 'a733_ufs_link.h', 'a733_ufs_board.c', 'a733_ufs_board.h', 'a733_ufs_handoff.c', 'a733_ufs_handoff.h', 'a733_ufs_quiesce.c', 'a733_ufs_quiesce.h', 'a733_ufs_nop.c', 'a733_ufs_nop.h', 'a733_ufs_nop_map.c', 'a733_ufs_readcheck.c', 'a733_ufs_readcheck.h', 'a733_ufs_scratch.c', 'a733_ufs_scratch.h', 'a733_ufs_write_wire.h', 'a733_hwdiag.c']:
        relative = 'vendor/allwinnertech/chips/a733/'+name
        assert files.count(relative) == 1
        original = (fixture/relative).read_bytes()
        (fixture/relative).write_bytes(original+b'\n')
        bad = subprocess.run([sys.executable, str(checker), str(fixture), '--ufs'],
            capture_output=True, text=True)
        assert bad.returncode != 0 and name in bad.stderr
        (fixture/relative).write_bytes(original)
    cmake = (chip/'CMakeLists.txt').read_text()
    assert 'list(APPEND SRCS a733_ufs.c a733_ufs_link.c a733_ufs_board.c a733_ufs_handoff.c a733_ufs_quiesce.c a733_ufs_nop.c a733_ufs_nop_map.c a733_ufs_readcheck.c a733_ufs_scratch.c a733_ufs_data.c)' in cmake
    assert 'CONFIG_A733_UFS_QUIESCE=y' in script
    assert 'CONFIG_A733_UFS_DMA_NOP=y' in script
    assert 'CONFIG_A733_UFS_READCHECK=y' in script
    assert 'CONFIG_A733_UFS_BLOCK_RO=y' in script
    assert 'CONFIG_A733_UFS_SCRATCH=y' in script
    assert 'CONFIG_A733_UFS_DATA=y' in script
    assert 'apps/nshlib/nsh_ddcmd.c' in script and 'nsh-dd-large-seek.patch' in script
    for relative in ['vendor/allwinnertech/chips/a733/a733_boot.c',
                     'vendor/allwinnertech/chips/a733/include/chip.h',
                     'vendor/allwinnertech/boards/a733/cubie-a7z/scripts/dramboot.ld']:
        assert files.count(relative)==1
        original=(fixture/relative).read_bytes()
        (fixture/relative).write_bytes(original+b'\n')
        bad=subprocess.run([sys.executable,str(checker),str(fixture),'--ufs'],capture_output=True,text=True)
        assert bad.returncode!=0 and relative in bad.stderr
        (fixture/relative).write_bytes(original)
    print('57 critical-source staging fixtures + CMake/wrapper/memory-map coverage PASS; real SDK untouched')
