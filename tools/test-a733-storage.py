#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Host fault tests using production utility + SDK mbedTLS SHA256, no HW."""
from pathlib import Path
import argparse,hashlib,os,subprocess,tempfile
p=argparse.ArgumentParser(description=__doc__);p.add_argument('sdk',type=Path);a=p.parse_args()
repo=Path(__file__).resolve().parent.parent
mb=a.sdk.resolve()/'apps/crypto/mbedtls/mbedtls'
with tempfile.TemporaryDirectory(prefix='a733-bulk-test-') as tmp:
    root=Path(tmp);(root/'nuttx').mkdir();(root/'nuttx/config.h').write_text('')
    (root/'data').mkdir();(root/'ufs').mkdir()
    (root/'test_config.h').write_text('#define MBEDTLS_SHA256_C\n')
    subprocess.run(['gcc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
        '-Wno-misleading-indentation','-fno-pie','-no-pie','-fsanitize=address,undefined',
        '-pthread','-I'+str(root),'-I'+str(mb/'include'),
        '-DMBEDTLS_CONFIG_FILE="test_config.h"',str(repo/'tools/test-a733-storage.c'),
        str(mb/'library/sha256.c'),str(mb/'library/platform_util.c'),'-o',str(root/'test')],check=True)
    result=subprocess.run([str(root/'test'),str(root)],check=True,timeout=45,
        text=True,capture_output=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1'})
    print(result.stdout,end='');print(result.stderr,end='')
    assert (root/'data/input').read_bytes()==(root/'ufs/good').read_bytes()
    digest=hashlib.sha256((root/'data/input').read_bytes()).hexdigest()
    assert 'SHA256='+digest in result.stdout
    print('Independent Python SHA256 match:',digest)
