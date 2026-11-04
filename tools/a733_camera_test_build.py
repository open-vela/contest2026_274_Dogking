# SPDX-License-Identifier: Apache-2.0
"""Build actual control adapter tests with the SDK's real BSD SHA2 core."""
from pathlib import Path
import shutil
import subprocess
def compile_control(repo,test,tmp,extra=()):
    chip=repo/'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733'
    sdk=repo.parent.parent/'quickly-openvela'
    include=Path(tmp)/'include/crypto';include.mkdir(parents=True)
    shutil.copyfile(sdk/'nuttx/include/crypto/sha2.h',include/'sha2.h')
    exe=Path(tmp)/'test'
    subprocess.run(['gcc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
        '-D_DEFAULT_SOURCE','-DFAR=','-include','stdint.h',
        '-Dswap32=__builtin_bswap32','-Dswap64=__builtin_bswap64',
        '-Wno-old-style-declaration', # SDK BSD SHA2 declaration style, not changed.
        '-fno-pie','-no-pie','-fsanitize=address,undefined',
        '-I'+str(repo/'tools/camera-control-host-fixture'),'-I'+str(chip),
        '-I'+str(Path(tmp)/'include'),
        *[str(chip/f'a733_imx214_{part}.c') for part in ('cold','twi3','probe','mode')],
        str(chip/'a733_mipi_rx.c'),
        str(chip/'a733_csi_capture.c'),str(chip/'a733_csi_frame_plan.c'),
        str(sdk/'nuttx/crypto/sha2.c'),str(repo/'tools'/test),*extra,'-o',str(exe)],
        check=True,timeout=30)
    return exe
