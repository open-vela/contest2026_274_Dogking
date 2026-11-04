#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Record exported bytes and vendor offline nbinfo, NEVER an ASR acceptance."""
import hashlib,json,sys,struct
from pathlib import Path
root=Path(sys.argv[1]).resolve(strict=True)
report=json.loads((root/'export-report.json').read_text())
assert len(report['models'])==3
for item in report['models']:
    assert item['returncode']==0
    role=item['role'];path=root/(role+'_nbg_unify/network_binary.nb')
    assert path.is_file() and not path.is_symlink() and 1024<path.stat().st_size<128*1024*1024
    raw=path.read_bytes()
    magic,version,cid=struct.unpack_from('<III',raw)
    assert magic==0x4e4d5056 and version in (0x10020,0x20000) and cid==0x1000003b,role
    item['header']={'magic':'%08x'%magic,'version':'%08x'%version,'cid':'%08x'%cid}
    item['nbg']=[{'path':str(path.relative_to(root)),'bytes':len(raw),
                  'sha256':hashlib.sha256(raw).hexdigest()}]
    info=root/(role+'-nbinfo.txt');text=info.read_text()
    assert len(text)>100 and 'Input Table' in text and 'Output Table' in text,role+': missing nbinfo'
    item['nbinfo_sha256']=hashlib.sha256(info.read_bytes()).hexdigest()
report['result']='EXPORTED_CID_CHECKED_NOT_BOARD_TESTED'
with (root/'artifacts-verified.json').open('x') as f:json.dump(report,f,indent=2)
print(json.dumps(report,indent=2))
