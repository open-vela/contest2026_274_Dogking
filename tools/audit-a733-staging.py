#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Read-only hash snapshot/check for the SDK paths temporarily staged by build."""
import argparse,hashlib,json,os,re
from pathlib import Path
def snapshot(sdk,repo):
    script=(repo/'tools/build-a733-wapi.sh').read_text();paths=set()
    for array in ('files','patched_files'):
        block=re.search(r'^'+array+r'=\(\n(.*?)^\)',script,re.M|re.S).group(1)
        paths.update(line.strip() for line in block.splitlines() if line.strip())
    for directory in ('apps/system/aipet','apps/system/aipetllm','apps/system/a733home','apps/system/a733storage'):
        root=sdk/directory;paths.add(directory)
        if root.is_dir() and not root.is_symlink():
            paths.update(p.relative_to(sdk).as_posix() for p in root.rglob('*'))
    entries={}
    for name in sorted(paths):
        p=sdk/name
        if p.is_symlink():entries[name]=dict(symlink=os.readlink(p))
        elif p.is_file():entries[name]=dict(bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
        elif p.is_dir():entries[name]=dict(directory=True)
        else:entries[name]=None
    return entries
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('mode',choices=['record','check']);p.add_argument('sdk',type=Path);p.add_argument('file',type=Path);a=p.parse_args()
    current=snapshot(a.sdk.resolve(strict=True),Path(__file__).resolve().parent.parent)
    if a.mode=='record':
        with a.file.open('x') as f:json.dump(current,f,indent=2,sort_keys=True)
        print('Recorded',len(current),'SDK paths')
    else:
        expected=json.loads(a.file.read_text());changed=[n for n in set(expected)|set(current) if expected.get(n)!=current.get(n)]
        if changed:p.exit(1,'SDK restoration mismatch: '+repr(sorted(changed))+'\n')
        print('SDK restoration byte-exact:',len(current),'paths')
