#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import subprocess,tempfile
from a733_camera_test_build import compile_control
r=Path(__file__).resolve().parents[1];chip=r/'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733'
with tempfile.TemporaryDirectory(prefix='stream205-') as d:
    d=Path(d);exe=d/'csi'
    subprocess.run(['gcc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fno-pie','-no-pie',
      '-fsanitize=address,undefined','-I'+str(chip),str(chip/'a733_csi_capture.c'),str(chip/'a733_csi_frame_plan.c'),
      str(r/'tools/test-a733-csi-stream-v205.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=30)
    exe=compile_control(r,'test-a733-camera-stream-v205.c',d/'broker',[
      '-Wl,-Ttext-segment=0x41000000','-DCONFIG_A733_CAMERA_PERF_TEST=1','-DCONFIG_A733_CAMERA_LIVE_TEST=1',
      '-I'+str(r/'tools/stream-host-fixture')])
    subprocess.run([str(exe),str(r/'out/v204-release-20261029/imx214-1080p.a7cm')],check=True,timeout=60)
