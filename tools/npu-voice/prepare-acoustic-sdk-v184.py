#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Copy only official ORT C API headers/license from the pinned source.

No model or vendor binary is copied into the repository. Download a separate
PyPI CPU runtime only when --host-oracle is requested; it stays private in out.
"""
import argparse
import hashlib
import json
from pathlib import Path
import tarfile
import urllib.parse
import urllib.request
import zipfile

REPO=Path(__file__).resolve().parents[2]
ARCHIVE_SHA='7270fab0d96d964959b35648014230c00fa2a9cb98df087e5fd5c2b2c24c8085'


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--headers',action='store_true')
    p.add_argument('--host-oracle',type=Path)
    a=p.parse_args()
    if a.headers:
        source=REPO/'out/onnxruntime-a83fc4d-source.tar.gz'
        assert hashlib.sha256(source.read_bytes()).hexdigest()==ARCHIVE_SHA
        dest=REPO/'board/a733-cubie-a7z/openvela-overlay/apps/system/aipet/third_party/onnxruntime'
        dest.mkdir()
        requested={'LICENSE':'LICENSE',
            'include/onnxruntime/core/session/onnxruntime_c_api.h':'include/onnxruntime_c_api.h',
            'include/onnxruntime/core/session/onnxruntime_ep_c_api.h':'include/onnxruntime_ep_c_api.h'}
        records={}
        with tarfile.open(source) as t:
            for m in t:
                name='/'.join(Path(m.name).parts[1:])
                if name not in requested:continue
                assert m.isfile() and m.size<600000
                data=t.extractfile(m).read();target=dest/requested[name]
                target.parent.mkdir(exist_ok=True)
                with target.open('xb') as f:f.write(data)
                records[requested[name]]=hashlib.sha256(data).hexdigest()
        assert len(records)==len(requested)
        (dest/'provenance.json').write_text(json.dumps(dict(project='ONNX Runtime',version='1.23.2',
            commit='a83fc4d58cb48eb68890dd689f94f28288cf2278',license='MIT',
            source_archive_sha256=ARCHIVE_SHA,files=records,models_included=False),indent=2))
        print('Pinned C API headers/license vendored, no models')
    if a.host_oracle:
        dest=a.host_oracle.resolve();assert dest.is_relative_to(REPO/'out');dest.mkdir()
        with urllib.request.urlopen('https://pypi.org/pypi/onnxruntime/1.23.2/json',timeout=30) as r:
            meta=json.load(r)
        wheels=[v for v in meta['urls'] if 'cp311-cp311-manylinux_2_27_x86_64' in v['filename']]
        assert len(wheels)==1
        entry=wheels[0]
        assert urllib.parse.urlparse(entry['url']).hostname=='files.pythonhosted.org'
        archive=dest/entry['filename']
        with urllib.request.urlopen(entry['url'],timeout=60) as r,archive.open('xb') as f:
            while block:=r.read(1024*1024):f.write(block)
        assert hashlib.sha256(archive.read_bytes()).hexdigest()==entry['digests']['sha256']
        extracted=[]
        with zipfile.ZipFile(archive) as z:
            for name in z.namelist():
                if name.startswith('onnxruntime/capi/') and ('libonnxruntime.so.' in name or name.endswith('libonnxruntime_providers_shared.so')):
                    target=dest/Path(name).name
                    with target.open('xb') as f:f.write(z.read(name))
                    extracted.append(dict(file=target.name,sha256=hashlib.sha256(target.read_bytes()).hexdigest()))
        assert any(v['file']=='libonnxruntime.so.1.23.2' for v in extracted)
        (dest/'provenance.json').write_text(json.dumps(dict(source='PyPI onnxruntime 1.23.2 CPU wheel',
            wheel=entry['filename'],wheel_sha256=entry['digests']['sha256'],files=extracted,
            purpose='HOST_C_API_ORACLE_ONLY_NOT_TARGET',target_runtime=False),indent=2))
        print('Host CPU C API oracle downloaded/hash-verified; NOT a target runtime')


if __name__=='__main__':main()
