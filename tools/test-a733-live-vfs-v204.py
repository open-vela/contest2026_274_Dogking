#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Real NPU path validation, both config branches; pinned SDK VFS contracts."""
from pathlib import Path
import hashlib,subprocess,tempfile
repo=Path(__file__).resolve().parents[1];chip=repo/'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733'
src=(chip/'a733_npu_runtime.c').read_text();start=src.index('static int a733_rt_validate_path(')
body=src[start:src.index('\n}\n',start)+3]
live=(repo/'board/a733-cubie-a7z/openvela-overlay/apps/system/a733camera/a733_camera_live.c').read_text()
assert '#define LIVE_ROOT "/camram-v204"' in live and 'errno!=ENOENT' in live
sdk=(repo/'../../quickly-openvela/nuttx').resolve()
mount=sdk/'fs/mount/fs_mount.c';reserve=sdk/'fs/inode/fs_inodereserve.c';unmount=sdk/'fs/mount/fs_umount2.c'
assert 'if (!INODE_IS_PSEUDODIR(mountpt_inode))' in mount.read_text()
assert "lies within a mountpoint" in reserve.read_text()
assert 'ret = inode_remove(target);' in unmount.read_text()
with tempfile.TemporaryDirectory(prefix='live-vfs204-') as tmp:
    tmp=Path(tmp);c=tmp/'test.c'
    c.write_text('''#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#define A733_NPU_MODEL_PATH_MAX 128
#define OK 0
'''+body+'''
int main(void){char p[128];const char *good[]={"/data/npu/model.a7pm","/data/npu/input.rgb"};
for(unsigned i=0;i<2;++i){strcpy(p,good[i]);assert(!a733_rt_validate_path(p));}
const char *ram[]={"/camram-v204/model.a7pm","/camram-v204/input.rgb"};
for(unsigned i=0;i<2;++i){strcpy(p,ram[i]);
#ifdef CONFIG_A733_CAMERA_LIVE_TEST
assert(!a733_rt_validate_path(p));
#else
assert(a733_rt_validate_path(p)==-EINVAL);
#endif
}
const char *bad[]={"/camram-v204/other","/camram-v204","/camram-v204X/input.rgb","/ufs/any", "/data/../x","/data/a\\\\b","/camram-v204/../input.rgb"};
for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i){strcpy(p,bad[i]);assert(a733_rt_validate_path(p)==-EINVAL);}
puts("PASS real path validator: old /data preserved; exactly two RAM files only with LIVE config");}
''')
    for flag in (False,True):
        exe=tmp/str(flag);args=['gcc','-std=c11','-D_DEFAULT_SOURCE','-Wall','-Wextra','-Werror','-fno-pie','-no-pie','-fsanitize=address,undefined']
        if flag:args+=['-DCONFIG_A733_CAMERA_LIVE_TEST=1']
        subprocess.run([*args,str(c),'-o',str(exe)],check=True,timeout=30);subprocess.run([str(exe)],check=True,timeout=10)
for f in (mount,reserve,unmount):print(f.name,hashlib.sha256(f.read_bytes()).hexdigest())
print('PASS SDK pseudofs-root mount / automatic unmount-inode-removal source contracts; no real mount claimed')
