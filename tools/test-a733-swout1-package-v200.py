#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise the real debugfs installer on NEW temporary regular files only."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile

repo=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('pack200',repo/'tools/package-a733-swout1-v200.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
seal=repo/'out/v199-release-20261028'
with tempfile.TemporaryDirectory(prefix='a733-v200-package-test-') as tmp:
    work=Path(tmp);rootfile=work/'root.ext4';assets=work/'assets'
    subprocess.run(['python3',str(repo/'tools/build-a733-swout1-v200.py'),str(seal),str(assets)],
                   check=True,capture_output=True,timeout=60)
    with rootfile.open('xb') as stream:stream.truncate(64*1024*1024)
    m.command(['mkfs.ext4','-q',rootfile])
    root=m.Root(rootfile,work)
    for d in ('/boot','/boot/extlinux','/boot/openvela','/boot/openvela/a733'):
        m.command(['debugfs','-w','-R','mkdir '+d,rootfile])
    original=work/'old.conf';original.write_text('label old\n linux /boot/openvela/a733/Image\n')
    root.write_new(m.ACTIVE,original)
    root.write_new('/boot/openvela/a733/Image',seal/'nuttx.bin')
    for name in m.ASSETS[3:]:root.write_new('/boot/openvela/a733/'+name,assets/name)
    # Shadowing earlier scripts MUST reject before any boot config mutation.
    shadow=work/'shadow';shadow.write_bytes(b'not executable')
    root.write_new('/boot.scr',shadow)
    try:m.install(root,assets)
    except ValueError:pass
    else:raise AssertionError('Shadow boot entry accepted')
    assert root.read(m.ACTIVE)==original.read_bytes() and not root.exists(m.BACKUP)
    m.command(['debugfs','-w','-R','rm /boot.scr',rootfile])
    result=m.install(root,assets)
    assert result['active_extlinux_absent'] and result['original_extlinux_preserved']
    assert root.read(m.BACKUP)==original.read_bytes()
    try:m.install(root,assets)
    except ValueError:pass
    else:raise AssertionError('Repeated install overwrote history')
    m.command(['e2fsck','-fn',rootfile])
    assert m.verify_root(root,assets,original.read_bytes())==result
print('PASS: real temporary ext4 installer/readback; kernel byte-identical; boot.scr alias; original extlinux retained; shadow/repeated installs rejected; NO PHYSICAL DEVICES')
