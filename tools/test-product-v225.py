#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Real deterministic intent/dispatcher under sanitizers; not physical acceptance."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
pet=r/'board/a733-cubie-a7z/openvela-overlay/apps/system/aipet'
with tempfile.TemporaryDirectory(prefix='product-v225-') as d:
    exe=Path(d)/'intent'
    subprocess.run(['g++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror',
        '-fsanitize=address,undefined','-I'+str(pet),
        '-I'+str(r/'board/a733-cubie-a7z/openvela-overlay/apps/include'),
        str(r/'tools/test-product-v225.cxx'),str(pet/'product_core.cxx'),
        str(pet/'mcu_protocol.cxx'),str(pet/'pet_core.cxx'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
