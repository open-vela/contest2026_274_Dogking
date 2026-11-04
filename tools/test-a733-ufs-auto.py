#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import os,subprocess,tempfile
repo=Path(__file__).resolve().parent.parent
overlay=repo/'board/a733-cubie-a7z/openvela-overlay'
with tempfile.TemporaryDirectory(prefix='a733-auto-') as tmp:
    p=Path(tmp);(p/'nuttx').mkdir();(p/'nuttx/config.h').write_text('')
    common=['-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fno-pie','-no-pie','-fsanitize=address,undefined']
    subprocess.run(['gcc','-std=c11',*common,'-pthread','-I'+str(p),'-I'+str(overlay/'apps/include'),str(repo/'tools/test-a733-storage-boot.c'),'-o',str(p/'boot')],check=True)
    subprocess.run(['g++','-std=c++17',*common,'-I'+str(overlay/'apps/system/aipet'),str(repo/'tools/test-aipet-model-path.cxx'),'-o',str(p/'model')],check=True)
    for name in ('boot','model'):
        subprocess.run([str(p/name)],check=True,timeout=30,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1'})
