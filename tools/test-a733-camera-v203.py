#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Production C with ASan/UBSan; real private inputs are read-only, never archived."""
from pathlib import Path
import subprocess,tempfile,zlib,hashlib,argparse,shutil
from a733_camera_test_build import compile_control
repo=Path(__file__).resolve().parents[1]
apps=repo/'board/a733-cubie-a7z/openvela-overlay/apps'
parser=argparse.ArgumentParser()
parser.add_argument('--private-raw-file',action='append',type=Path,default=[])
parser.add_argument('--private-output',type=Path)
args=parser.parse_args()
if args.private_raw_file:
    assert args.private_output and not args.private_output.exists()
    args.private_output.mkdir(parents=True)
with tempfile.TemporaryDirectory(prefix='a733-203-') as tmp:
    root=Path(tmp)
    tone=root/'tone'
    subprocess.run(['gcc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fno-pie','-no-pie',
      '-fsanitize=address,undefined','-I'+str(apps/'include'),str(repo/'tools/test-a733-camera-tone-v203.c'),
      '-o',str(tone)],check=True)
    subprocess.run([str(tone)],check=True,timeout=10)
    exe=compile_control(repo,'test-a733-camera-repeat-v203.c',root,['-Wl,-Ttext-segment=0x41000000'])
    subprocess.run([str(exe),str(repo/'out/imx214-mode-v187-r1/imx214-1080p.a7cm')],check=True,timeout=60)
    export=root/'exporter'
    subprocess.run(['gcc','-std=c11','-D_DEFAULT_SOURCE','-DCONFIG_A733_CAMERA_CONTROL_TEST=1',
      '-O1','-g','-Wall','-Wextra','-Werror','-fno-pie','-no-pie','-fsanitize=address,undefined',
      '-I'+str(apps/'include'),'-I'+str(repo/'tools/camera-host-fixture'),
      str(repo/'tools/test-a733-camera-export.c'),str(apps/'system/a733camera/a733_camera_image.c'),
      '-o',str(export)],check=True)
    for order in ('bggr','rggb','grbg','gbrg'):
        out=root/order
        subprocess.run([str(export),'capture',str(out),order],check=True,capture_output=True,timeout=30)
        assert len((out/'view.rgb').read_bytes())==61440
        assert (out/'view.ppm').read_bytes()==b'P6\n128 160\n255\n'+(out/'view.rgb').read_bytes()
        v=(out/'vision-rgb.bin').read_bytes();assert len(v)==1228800
        assert v[:640*140*3]==bytes([114])*(640*140*3) and v[-640*140*3:]==bytes([114])*(640*140*3)
        assert f'vision-input-crc32={(zlib.crc32(v,0xffffffff)^0xffffffff):08x}' in (out/'VIEW.txt').read_text()
        assert 'rotation=180' in (out/'VIEW.txt').read_text()
    for invalid in [('tone','256','256','256','256','256','180'),('tone','64','0','256','256','256','180'),
                 ('tone','64','256','256','256','256','90'),('tone','64','256','256','256','256','-1')]:
        assert subprocess.run([str(export),*invalid],capture_output=True).returncode!=0
    assert subprocess.run([str(export),'tone','0','256','256','256','256','0'],capture_output=True).returncode==0
    for i,rawfile in enumerate(args.private_raw_file):
        before=hashlib.sha256(rawfile.read_bytes()).hexdigest()
        output=root/f'private-{i}'
        run=subprocess.run([str(export),'process',str(rawfile),str(output)],check=True,capture_output=True,text=True,timeout=30)
        assert 'NOT a new camera capture' in run.stdout
        assert 'user-supplied-raw-file' in (output/'COMPLETE.txt').read_text()
        assert hashlib.sha256(rawfile.read_bytes()).hexdigest()==before
        for name in ('view.ppm','view.rgb','VIEW.txt'):
            shutil.copyfile(output/name,args.private_output/f'{i}-{name}')
        print('PRIVATE fixture',i,'RAW SHA',before,'production C processing passed; NOT camera acceptance')
print('PASS v203: four Bayer enhanced exports, padding/CRC, manual tone bounds, legacy originals retained')
