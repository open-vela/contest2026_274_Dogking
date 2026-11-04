#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Primary sizing-mode facts + archived v197/v198 reported-failure replay."""
from pathlib import Path
import hashlib,re,subprocess,tempfile
repo=Path(__file__).resolve().parents[1]
chip=repo/'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733'
primary=repo.parent/'bsp-product-aiot-stable/drivers/vin/vin-video/dma140'
header=(primary/'dma140_reg_i.h').read_text();driver=(primary/'dma140_reg.c').read_text()
for name,value in [('CSIC_DMA_TOP_EN_REG_OFF',0),('CSIC_BUF_LENGTH_CFG_MODE',29),
                   ('CSIC_FLIP_SIZE_CFG_MODE',28),('CSIC_VFLIP_BUF_ADDR_CFG_MODE',30),
                   ('CSIC_DMA_BUF_LEN_REG_OFF',0x238),('CSIC_DMA_FLIP_SIZE_REG_OFF',0x23c)]:
    actual=re.search(r'^#define\s+'+name+r'\s+(0[xX][0-9a-fA-F]+|[0-9]+)\s*$',header,re.M)
    assert actual and int(actual[1],0)==value,name
assert re.search(r'CSIC_BUF_LENGTH_CFG_MODE_MASK,\s*en\s*<<\s*CSIC_BUF_LENGTH_CFG_MODE',driver)
assert re.search(r'CSIC_FLIP_SIZE_CFG_MODE_MASK,\s*en\s*<<\s*CSIC_FLIP_SIZE_CFG_MODE',driver)
assert re.search(r'CSIC_VFLIP_BUF_ADDR_CFG_MODE_MASK,\s*en\s*<<\s*CSIC_VFLIP_BUF_ADDR_CFG_MODE',driver)
video=(primary.parent/'vin_video.c').read_text()
assert 'csic_dma_buf_length_software_enable(logic_vinc->vipp_sel, 0)' in video
assert 'csi_dam_flip_software_enable(logic_vinc->vipp_sel, 0)' in video
relative=str((chip/'a733_csi_capture.c').relative_to(repo))
for version,commit in [(197,'947cb9c133f5b9fd45bd23fbae9cd64efaa6e503'),
                       (198,'735c1ddd9c1c9a5fe62ccbfb0cf614156f774d9e')]:
    with tempfile.TemporaryDirectory(prefix=f'a733-v{version}-sizing-replay-') as tmp:
        source=Path(tmp)/'legacy.c';exe=Path(tmp)/'test'
        data=subprocess.check_output(['git','show',commit+':'+relative],cwd=repo)
        import json
        evidence=json.loads((repo/f'docs/evidence/camera-v{version}-final/summary.json').read_text())
        assert hashlib.sha256(data).hexdigest()==evidence['firmware_source_sha256'][relative]
        source.write_bytes(data)
        subprocess.run(['gcc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
          '-fno-pie','-no-pie','-fsanitize=address,undefined','-I'+str(chip),
          str(source),str(chip/'a733_csi_frame_plan.c'),str(repo/'tools/test-a733-csi-capture.c'),
          '-o',str(exe)],check=True)
        subprocess.run([str(exe),'legacy'],check=True,timeout=30)
print('PASS: primary TOP mode facts, BOTH archived source identities, exact board first/cleanup signatures; HOST MODEL ONLY')
