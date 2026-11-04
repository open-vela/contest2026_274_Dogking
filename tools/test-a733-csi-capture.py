#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import subprocess,tempfile
repo=Path(__file__).resolve().parents[1]
chip=repo/'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733'
with tempfile.TemporaryDirectory(prefix='a733-csi-frame-') as tmp:
    exe=Path(tmp)/'test'
    subprocess.run(['gcc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
        '-fno-pie','-no-pie','-fsanitize=address,undefined','-I'+str(chip),
        str(chip/'a733_csi_capture.c'),str(chip/'a733_csi_frame_plan.c'),
        str(repo/'tools/test-a733-csi-capture.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=40)
