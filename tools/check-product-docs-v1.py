#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check current Markdown links/fences and preserve the original summary body."""
from pathlib import Path
import argparse,hashlib,json,re,tarfile
r=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
files=['docs/PROJECT_FULL_SUMMARY.md','docs/PROJECT_MANUAL_V1_0.md',
       'docs/POST_PRELIMINARY_IMPROVEMENTS.md','docs/a733/PRODUCT_V1_0.md',
       'docs/a733/PRODUCT_V1_0_AUDIT.md','docs/a733/PRODUCT_V1_0_RELEASE.md',
       'docs/evidence/product-v1-audit/README.md']
records={}
for name in files:
    f=r/name;data=f.read_bytes();s=data.decode('utf-8');assert '\ufffd' not in s,name
    fenced=False
    for line in s.splitlines():
        if line.startswith('```'):fenced=not fenced
    assert not fenced,name
    links=[]
    for target in re.findall(r'\[[^\]\n]+\]\(([^)\n]+)\)',s):
        if '://' in target or target.startswith('#'):continue
        target=target.strip('<>').split('#',1)[0]
        assert (f.parent/target).is_file(),(name,target)
        links.append(target)
    records[name]={'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'local_links':links}
oldname='docs/PROJECT_FULL_SUMMARY_20261030_V211.md'
with tarfile.open(r/'out/checkpoints/a733-v1-product-recovery-20261031/project.tar.gz','r:gz') as archive:
    old=archive.extractfile(oldname).read().decode('utf-8').replace('\r\n','\n')
new=(r/oldname).read_text(encoding='utf-8').replace('\r\n','\n')
notice=re.search(r'^> 2026-10-31 更新：.*\n\n',new,re.M);assert notice
assert new[:notice.start()]+new[notice.end():]==old,'Original historical summary body changed'
result={'result':'CURRENT_DOCS_LINKS_UTF8_FENCES_AND_HISTORICAL_BODY_PASSED',
        'documents':records,'historical_body_preserved':True,'hardware_verified':False}
out=a.output.resolve();assert out.is_relative_to(r/'out') and not out.exists();out.mkdir()
(out/'summary.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
print(json.dumps(result,ensure_ascii=False,indent=2))
