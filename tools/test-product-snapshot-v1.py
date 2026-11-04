#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Real camera coordinator, MOCK devices: fresh one-shot token/cleanup proof."""
from pathlib import Path
import tempfile,subprocess,struct,os
r=Path(__file__).resolve().parents[1];overlay=r/'board/a733-cubie-a7z/openvela-overlay';apps=overlay/'apps'
source=(r/'tools/test-a733-live-v204.c').read_text().split('\nint main(')[0]
original='../board/a733-cubie-a7z/openvela-overlay/apps/system/a733camera/a733_camera_live.c'
source=source.replace(original,str(apps/'system/a733camera/a733_camera_live.c'))
source+=r'''
static int product_reserved,product_gate_held;
int a733_product_camera_begin(void) {
  if(product_reserved)return -EBUSY;
  assert(!product_gate_held);product_gate_held=1;return 0;
}
void a733_product_start_unlock(void) {
  assert(product_gate_held);product_gate_held=0;
}
int main(int argc,char **argv) {
  assert(argc==3);uint32_t a=0,b=0;struct a733_live_result result={0};
  product_reserved=1;
  assert(a733_camera_identify_start(argv[1],&a)==-EBUSY&&!a&&!a733_camera_live_busy());
  assert(!product_gate_held&&!pending_task);product_reserved=0;
  assert(a733_camera_identify_poll(99,&result)==-ESTALE);
  assert(!a733_camera_identify_start(argv[1],&a)&&a);
  assert(a733_camera_identify_poll(a,&result)==-EAGAIN);
  assert(a733_camera_identify_stop(a+1)==-ESTALE);
  assert(a733_camera_identify_start(argv[1],&b)==-EBUSY);
  assert(!product_gate_held);
  test_task(0,NULL);assert(!a733_camera_live_busy());
  if(!strcmp(argv[2],"bad-crc")||!strcmp(argv[2],"bad-irq")) {
    assert(a733_camera_identify_poll(a,&result)==-EPROTO&&!identify_ready);
  } else if(!strcmp(argv[2],"cleanup")) {
    assert(a733_camera_identify_poll(a,&result)==-EOWNERDEAD&&a733_camera_live_blocked());
    assert(a733_camera_identify_start(argv[1],&b)==-EIO);
  } else {
    assert(!a733_camera_identify_poll(a,&result)&&!a733_live_result_valid(&result));
    assert(job.frames==1&&job.inferences==1&&captures==1&&displayed==1&&inferred==1);
    assert(!a733_camera_identify_start(argv[1],&b)&&b!=a);
    assert(a733_camera_identify_poll(a,&result)==-ESTALE);
    test_task(0,NULL);assert(!a733_camera_identify_poll(b,&result));
    assert(captures==2&&displayed==2&&inferred==2&&job.frames==1);
    assert(!a733_camera_identify_start(argv[1],&a)&&a!=b);
    assert(!a733_camera_identify_stop(a));test_task(0,NULL);
    assert(a733_camera_identify_poll(a,&result)==-ENODATA); // No preceding result reused after stop.
    assert(!a733_camera_live_blocked());
  }
  puts("PASS actual snapshot API: one fresh same-frame result per job, unique tokens, stale/stop/CRC/IRQ rejection and cleanup quarantine; MOCK DEVICES, NOT sensor/NPU acceptance.");
}
'''
with tempfile.TemporaryDirectory(prefix='product-snapshot-') as tmp:
    t=Path(tmp);fixture=t/'snapshot.c';fixture.write_text(source)
    model=t/'model.a7pm';data=bytearray(360);struct.pack_into('<12I',data,0,0x4d503741,3,128,0x1000003b,1,0,0,0,1,0,len(data),0);data[48:64]=b'yolov8n_6_pcq_a\0';model.write_bytes(data)
    exe=t/'snapshot';root=t/'ram'
    subprocess.run(['gcc','-std=c11','-D_DEFAULT_SOURCE','-DCONFIG_A733_CAMERA_PERF_TEST=1','-DCONFIG_A733_PRODUCT_V1=1','-O1','-g','-Wall','-Wextra','-Werror',
      '-Wno-unused-function','-fno-pie','-no-pie','-fsanitize=address,undefined','-pthread','-DLIVE_ROOT="'+str(root)+'"',
      '-I'+str(apps/'include'),'-I'+str(r/'tools/camera-host-fixture'),'-I'+str(r/'tools/live-host-fixture'),
      '-I'+str(overlay/'nuttx/include'),str(fixture),str(apps/'system/a733camera/a733_camera_image.c'),'-o',str(exe)],check=True)
    for name,env in [('pass',{}),('bad-crc',{'FAIL_CRC':'1'}),('bad-irq',{'FAIL_IRQ':'1'}),('cleanup',{'FAIL_UMOUNT':'1'})]:
        subprocess.run([str(exe),str(model),name],env={**os.environ,**env},check=True,timeout=30)
