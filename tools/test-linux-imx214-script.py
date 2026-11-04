#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Inspect-only script fixture; no camera, Linux configuration or register I/O."""
from pathlib import Path
import hashlib,json,os,subprocess,tarfile,tempfile
script=Path(__file__).resolve().with_name('test-linux-imx214.sh')
with tempfile.TemporaryDirectory(prefix='imx214-script-') as tmp:
    root=Path(tmp);bin=root/'bin';bin.mkdir()
    stub='#!/bin/sh\nprintf "fixture tool %s\\n" "$0"\nexit 0\n'
    for name in ['uname','v4l2-ctl','media-ctl','gst-inspect-1.0','dmesg','cat']:
        f=bin/name;f.write_text(stub);f.chmod(0o755)
    forbidden=bin/'gst-launch-1.0'
    forbidden.write_text('#!/bin/sh\ntouch "$FORBIDDEN_MARK"\nexit 1\n');forbidden.chmod(0o755)
    env={**os.environ,'PATH':str(bin)+':'+os.environ['PATH'],'FORBIDDEN_MARK':str(root/'stream-started')}
    subprocess.run(['bash','-n',str(script)],check=True)
    result=subprocess.run(['bash',str(script),'--output',str(root)],env=env,
                          check=True,capture_output=True,text=True,timeout=30)
    dirs=[p for p in root.glob('imx214-report-*') if p.is_dir()];assert len(dirs)==1
    report=dirs[0];archive=Path(str(report)+'.tar.gz')
    assert archive.is_file() and not (root/'stream-started').exists()
    assert 'NOT requested' in (report/'capture-status.txt').read_text()
    assert not list(report.glob('*.partial')) and 'device-tree.json' in json.loads((report/'sha256.json').read_text())
    for name,entry in json.loads((report/'sha256.json').read_text()).items():
        assert (report/name).stat().st_size==entry['bytes']
        assert hashlib.sha256((report/name).read_bytes()).hexdigest()==entry['sha256']
    with tarfile.open(archive) as tar:
        names=tar.getnames()
        assert len(names)==len(set(names)) and all(n.startswith(report.name+'/') or n==report.name for n in names)
    bad=subprocess.run(['bash',str(script),'--video','/dev/sda'],env=env,capture_output=True,timeout=10)
    assert bad.returncode!=0 and not (root/'stream-started').exists()
    print('Linux IMX214 collector PASS: inspect-only/no streaming, invalid device rejected, new report/archive/SHA256; fake commands only')
