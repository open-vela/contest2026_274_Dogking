#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Read-only real bundles vs production C; archive hashes/log, never photographs."""
from pathlib import Path
from array import array
import sys
import argparse,hashlib,json,shutil,subprocess,tempfile,zlib
repo=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('first',type=Path);p.add_argument('second',type=Path)
p.add_argument('--serial',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
a=p.parse_args();text=a.serial.read_text(encoding='utf-8-sig')
assert 'repeat-v203: generation=1 readers=0 ready=1' in text
assert 'repeat-v203: generation=2 readers=0 ready=1' in text
assert text.count('frame-experiment')>=2 and text.count('published=1 validation=0')>=2
for token in ('fingerprint=5b9d2742','fingerprint=7a12fbec','chip-id=0214 match=1 error=0',
              'frame-lines=2128 exposure-lines=2118 analog-code=0000'):
    assert token in text,token
apps=repo/'board/a733-cubie-a7z/openvela-overlay/apps'
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
records=[]
with tempfile.TemporaryDirectory(prefix='real-v203-') as tmp:
    tmp=Path(tmp);exe=tmp/'exporter'
    subprocess.run(['gcc','-std=c11','-D_DEFAULT_SOURCE','-DCONFIG_A733_CAMERA_CONTROL_TEST=1',
      '-O1','-g','-Wall','-Wextra','-Werror','-fno-pie','-no-pie','-fsanitize=address,undefined',
      '-I'+str(apps/'include'),'-I'+str(repo/'tools/camera-host-fixture'),
      str(repo/'tools/test-a733-camera-export.c'),str(apps/'system/a733camera/a733_camera_image.c'),
      '-o',str(exe)],check=True,timeout=30)
    for index,directory in enumerate((a.first,a.second)):
        actual=[f for f in directory.iterdir() if f.is_file()]
        files={f.name.casefold():f for f in actual}
        assert len(files)==len(actual)==9
        before={name:sha(f) for name,f in files.items()}
        raw=files['frame.raw'];assert raw.stat().st_size==4147200
        words=array('H');words.frombytes(raw.read_bytes())
        if sys.byteorder!='little':words.byteswap()
        fingerprint=2166136261
        for word in words:
            assert word<=1023
            fingerprint=((fingerprint^word)*16777619)&0xffffffff
        fingerprint=f'{fingerprint:08x}'
        assert 'fingerprint='+fingerprint in text,'RAW does not match board fingerprint'
        marker=files['complete.txt'].read_text();view=files['view.txt'].read_text()
        assert 'origin=real-published-DMA-frame' in marker
        crc=lambda f:f'{(zlib.crc32(f.read_bytes(),0xffffffff)^0xffffffff):08x}'
        assert 'source-raw-crc32='+crc(raw) in view
        assert 'vision-input-crc32='+crc(files['vision-rgb.bin']) in view
        assert 'model-input-crc32='+crc(files['model-rgb.bin']) in marker
        result=tmp/str(index)
        run=subprocess.run([str(exe),'process',str(raw),str(result)],capture_output=True,text=True,check=True,timeout=30)
        assert 'NOT a new camera capture' in run.stdout
        for name in ('frame.raw','model-rgb.bin','preview.rgb','preview.ppm','view.rgb','view.ppm','vision-rgb.bin','VIEW.txt'):
            assert (result/name).read_bytes()==files[name.casefold()].read_bytes(),name
        assert before=={name:sha(f) for name,f in files.items()},'Source files changed'
        records.append(dict(directory=directory.name,sha256=before,raw_crc32=crc(raw),board_fingerprint=fingerprint,
            model_crc32=crc(files['model-rgb.bin']),vision_crc32=crc(files['vision-rgb.bin']),
            complete=marker,tone=view,production_c_byte_exact=True,source_unchanged=True))
assert records[0]['sha256']['frame.raw']!=records[1]['sha256']['frame.raw']
out=a.output.resolve();assert out.is_relative_to(repo/'docs/evidence') and not out.exists()
out.mkdir(parents=True);shutil.copyfile(a.serial,out/'serial.txt')
result=dict(result='USER_REPORTED_TWO_REAL_CAPTURES_AND_DISPLAY_PASS_C_REPLAY_MATCHED',
    source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip(),
    serial_sha256=sha(a.serial),bundles=records,photographs_archived=False,
    host_replay_is_not_new_capture=True,reported_colour_normal=True,
    cold_boot_loops=False,continuous_video=False,isp_ae_awb=False,yolo_accuracy_tested=False)
(out/'audit.json').write_text(json.dumps(result,indent=2)+'\n')
print('PASS: two different real RAW bundles; eight payloads each match production C; originals unchanged; no photos archived')
