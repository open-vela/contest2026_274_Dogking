#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile current live code against the actual old batch FIR; no board."""
import subprocess
import tempfile
from pathlib import Path
repo=Path(__file__).resolve().parents[1]
base=repo/'board/a733-cubie-a7z/openvela-overlay'
pet=base/'apps/system/aipet'
source=(pet/'audio_app.cxx').read_text()
start=source.index('int audio_decimate(')
end=source.index('\n}',start)+2
with tempfile.TemporaryDirectory(prefix='a733-live182-') as tmp:
    oracle=Path(tmp)/'batch.cxx'
    oracle.write_text('#include <cstddef>\n#include <cerrno>\n#include <cmath>\n#include <cstdint>\nnamespace aipet {\n'+source[start:end]+'\n}\n')
    exe=Path(tmp)/'test'
    subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror',
        '-fsanitize=address,undefined','-pthread','-I'+str(pet),'-I'+str(base/'apps/include'),
        str(repo/'tools/test-live-capture-v182.cxx'),str(pet/'audio_capture_live.cxx'),str(oracle),
        '-lm','-Wl,--wrap=pthread_join','-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
