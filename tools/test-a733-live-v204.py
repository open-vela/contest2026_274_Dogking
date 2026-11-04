#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile actual background coordinator and camera broker; no physical devices."""
from pathlib import Path
import os,struct,subprocess,tempfile,sys
from a733_camera_test_build import compile_control
repo=Path(__file__).resolve().parents[1];apps=repo/'board/a733-cubie-a7z/openvela-overlay/apps'
with tempfile.TemporaryDirectory(prefix='live204-') as tmp:
    tmp=Path(tmp);exe=tmp/'live';root=tmp/'ram';model=tmp/'model.a7pm'
    dummy=bytearray(360);struct.pack_into('<12I',dummy,0,0x4d503741,3,128,0x1000003b,1,0,0,0,1,0,len(dummy),0)
    dummy[48:64]=b'yolov8n_6_pcq_a\0';model.write_bytes(dummy) # Valid header ONLY, never an executable NPU model.
    wrong=tmp/'wrong.a7pm';bad=bytearray(dummy);bad[48]^=1;wrong.write_bytes(bad)
    subprocess.run(['gcc','-std=c11','-D_DEFAULT_SOURCE','-O1','-g','-Wall','-Wextra','-Werror','-fno-pie','-no-pie',
      '-fsanitize=address,undefined','-pthread','-DLIVE_ROOT="'+str(root)+'"',
      '-I'+str(apps/'include'),'-I'+str(repo/'tools/camera-host-fixture'),
      '-I'+str(repo/'tools/live-host-fixture'),'-I'+str(repo/'board/a733-cubie-a7z/openvela-overlay/nuttx/include'),
      *(['-DCONFIG_A733_CAMERA_PERF_TEST=1'] if '--perf' in sys.argv else []),
      str(repo/'tools/test-a733-live-v204.c'),str(apps/'system/a733camera/a733_camera_image.c'),'-o',str(exe)],check=True,timeout=30)
    for mode,env in [('pass',{}),('stop-before',{}),('bad-crc',{'FAIL_CRC':'1'}),
                     ('capture-error',{'FAIL_CAPTURE':'1'}),('mount-error',{'FAIL_MOUNT':'1'}),
                     ('bad-irq',{'FAIL_IRQ':'1'}),('lcd-busy',{'FAIL_LCD':'1'}),('task-error',{'FAIL_TASK':'1'}),
                     ('model-missing',{}),('model-wrong',{}),('fstype-error',{'FAIL_FSTYPE':'1'}),('cleanup-error',{'FAIL_UMOUNT':'1'})]:
        input_model=tmp/'absent' if mode=='model-missing' else wrong if mode=='model-wrong' else model
        r=subprocess.run([str(exe),str(input_model),mode],capture_output=True,text=True,timeout=15,env={**os.environ,**env})
        assert not r.returncode,r.stdout+r.stderr
        print(mode+': '+(r.stdout.splitlines()[-1] if r.stdout else 'launch refused with no hardware'))
        if mode in ('fstype-error','cleanup-error'):
            assert root.exists() and not list(root.iterdir())
            root.rmdir() # MOCK mount only; exact private tempfile root, never a real mount.
        else:assert not root.exists()
    if '--pipe' in sys.argv:
        for mode,extra in [('pass',{}),('stop-before',{}),('bad-crc',{'FAIL_CRC':'1'}),
           ('bad-irq',{'FAIL_IRQ':'1'}),('capture-error',{'FAIL_CAPTURE':'1'}),
           ('second-capture-error',{'FAIL_SECOND_CAPTURE':'1'}),
           ('second-capture-error',{'FAIL_SECOND_CAPTURE':'1','FAIL_STREAM_STOP':'1'}),
           ('pass',{'STOP_AFTER_FRAME':'1'}),('pass',{'SLOW_DISPLAY':'1'}),
           ('thread-error',{'FAIL_PTHREAD':'1'}),('stop-capture',{'STOP_DURING_CAPTURE':'1'})]:
            q=subprocess.run([str(exe),str(model),mode],capture_output=True,text=True,timeout=20,
              env={**os.environ,'TEST_PIPE':'1',**extra})
            assert not q.returncode,q.stdout+q.stderr
            assert not root.exists()
            print('PIPE PASS',mode,extra)
    if '--async' in sys.argv:
        for mode,extra in [('slot-snapshot',{}),('pass',{}),('stop-before',{}),
           ('bad-crc',{'FAIL_CRC':'1'}),('bad-irq',{'FAIL_IRQ':'1'}),
           ('capture-error',{'FAIL_CAPTURE':'1'}),('second-capture-error',{'FAIL_SECOND_CAPTURE':'1'}),
           ('second-capture-error',{'FAIL_SECOND_CAPTURE':'1','FAIL_STREAM_STOP':'1'}),
           ('pass',{'STOP_AFTER_FRAME':'1'}),('pass',{'SLOW_DISPLAY':'1','SLOW_NPU':'1','REQUIRE_OVERLAP':'1'}),
           ('thread-error',{'FAIL_PTHREAD':'1'}),('thread-error',{'FAIL_SECOND_THREAD':'1'}),
           ('lcd-busy',{'FAIL_LCD':'1'}),('render-error',{'FAIL_RENDER':'1'}),
           ('second-render-error',{'FAIL_SECOND_RENDER':'1'}),('display-end-error',{'FAIL_DISPLAY_END':'1'}),
           ('stop-capture',{'STOP_DURING_CAPTURE':'1'}),
           ('pass',{'TEST_ASYNC_VERIFY':'1'})]:
            q=subprocess.run([str(exe),str(model),mode],capture_output=True,text=True,timeout=20,
              env={**os.environ,'TEST_ASYNC':'1',**extra})
            assert not q.returncode,q.stdout+q.stderr
            assert not root.exists()
            print('ASYNC PASS',mode,extra)
    broker=compile_control(repo,'test-a733-camera-budget-v204.c',tmp/'broker',['-Wl,-Ttext-segment=0x41000000'])
    if '--perf' in sys.argv:
        for mode,env in [('pass',{'TEST_STREAM':'1'}),('capture-error',{'TEST_STREAM':'1','FAIL_CAPTURE':'1'}),('pass',{'TEST_LEGACY':'1'}),
                         ('pass',{'TEST_VERIFY':'1'}),('verify-fail',{'TEST_VERIFY':'1','FAIL_VERIFY':'1'}),
                         ('second-capture-error',{'TEST_STREAM':'1','FAIL_SECOND_CAPTURE':'1'}),
                         ('second-capture-error',{'TEST_STREAM':'1','FAIL_SECOND_CAPTURE':'1','FAIL_STREAM_STOP':'1'})]:
            p=subprocess.run([str(exe),str(model),mode],capture_output=True,text=True,timeout=15,env={**os.environ,**env})
            assert not p.returncode,p.stdout+p.stderr
            assert not root.exists()
    subprocess.run([str(broker),str(repo/'out/imx214-mode-v187-r1/imx214-1080p.a7cm')],check=True,timeout=90)
print('PASS actual v204 coordinator and broker host tests; NOT hardware/video/NPU inference acceptance')
