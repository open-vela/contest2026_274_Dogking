#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import os,subprocess,tempfile,zlib
repo=Path(__file__).resolve().parents[1]
apps=repo/'board/a733-cubie-a7z/openvela-overlay/apps'
with tempfile.TemporaryDirectory(prefix='a733-export-') as tmp:
    root=Path(tmp);exe=root/'test'
    subprocess.run(['gcc','-std=c11','-D_DEFAULT_SOURCE','-DCONFIG_A733_CAMERA_CONTROL_TEST=1',
      '-O1','-g','-Wall','-Wextra','-Werror','-fno-pie','-no-pie','-fsanitize=address,undefined',
      '-I'+str(apps/'include'),'-I'+str(repo/'tools/camera-host-fixture'),
      str(repo/'tools/test-a733-camera-export.c'),str(apps/'system/a733camera/a733_camera_image.c'),
      '-o',str(exe)],check=True)
    def run(*args,good=True,extra=None):
        r=subprocess.run([str(exe),*map(str,args)],capture_output=True,text=True,
            env={**os.environ,**(extra or {})},timeout=30)
        assert (r.returncode==0)==good,r.stdout+r.stderr
        return r
    r=run('boot-help');assert 'camera-v203.scr' in r.stdout and 'mmc part' in r.stdout
    for order in ('bggr','rggb','grbg','gbrg'):
        out=root/order;run('capture',out,order)
        assert (out/'frame.raw').stat().st_size==3840*1080
        assert (out/'preview.rgb').stat().st_size==128*160*3
        assert (out/'preview.ppm').read_bytes()==b'P6\n128 160\n255\n'+(out/'preview.rgb').read_bytes()
        assert ('pixel=raw10le-'+order) in (out/'COMPLETE.txt').read_text()
        model=(out/'model-rgb.bin').read_bytes();assert len(model)==640*640*3
        assert f'model-input-crc32={(zlib.crc32(model,0xffffffff)^0xffffffff):08x}' in (out/'COMPLETE.txt').read_text()
        assert model[:640*140*3]==bytes([114])*(640*140*3)
        assert model[-640*140*3:]==bytes([114])*(640*140*3)
        before=(out/'frame.raw').read_bytes();run('capture',out,order,good=False)
        assert (out/'frame.raw').read_bytes()==before
    run('export',root/'export')
    for name,extra in (('not-ready',{'FRAME_NOT_READY':'1'}),('short',{'FRAME_SHORT':'1'}),
                       ('error',{'FRAME_CAPTURE_ERROR':'1'})):
        out=root/name;run('capture',out,good=False,extra=extra);assert not out.exists()
    run('capture',root/'bad','bogus',good=False);assert not (root/'bad').exists()
    out=root/'failed-marker';run('capture',out,good=False,extra={'FAIL_MARKER_WRITE':'1'})
    assert (out/'COMPLETE.partial').exists() and not (out/'COMPLETE.txt').exists()
    out=root/'no-handoff';r=run('capture',out,good=False,extra={'FRAME_NO_HANDOFF':'1'})
    assert not out.exists() and 'camera-v203.scr' in r.stdout and 'do NOT run display/NPU' in r.stderr
print('PASS: actual capture/export FILE CLI, four Bayer previews, SAME-RAW model tensor/padding/CRC, strict device/size, no overwrite; SIMULATED DEVICE ONLY')
