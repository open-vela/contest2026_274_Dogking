#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Record verified source-only camera evidence in a NEW documentation folder."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('check', type=Path)
p.add_argument('destination', type=Path)
a = p.parse_args()
repo = Path(__file__).resolve().parent.parent
sha = lambda f: hashlib.sha256(f.read_bytes()).hexdigest()
record = json.loads((a.check / 'summary.json').read_text())
assert record['result'] == 'CAMERA_V186_ARM_OBJECTS_AND_SYNTHETIC_TESTS_PASSED'
for name, expected in {**record['headers'], **record['test_sources'],
                       **record['integration']}.items():
    assert sha(repo / name) == expected, 'Stale source: ' + name
for item in record['files']:
    assert sha(repo / item['source']) == item['source_sha256']
    assert sha(a.check / (Path(item['source']).stem + '.o')) == item['object_sha256']
for name, expected in record['logs'].items():
    assert sha(a.check / name) == expected, 'Stale log: ' + name

references = [
    '../radxa_cubie_a7z_schematic_v1.11.pdf',
    '../bsp-product-aiot-stable/drivers/clk/sunxi-ng/ccu-sun60iw2.c',
    '../bsp-product-aiot-stable/drivers/clk/sunxi-ng/ccu_mp.c',
    '../bsp-product-aiot-stable/drivers/pinctrl/pinctrl-sun60iw2.c',
    '../bsp-product-aiot-stable/configs/linux-6.1/sun60iw2p1.dtsi',
    '../bsp-product-aiot-stable/drivers/vin/platform/sun60iw2_vin_cfg.h',
    'out/imx214-reference-v176/imx214-upstream.c']
record['reference_sha256'] = {name: sha(repo / name) for name in references}
record['source_baseline'] = 'c982d2a'
record['voice_image_preserved'] = {
    'file': '../openvela-a733-cubie-a7z-sd-v185-general-voice-candidate.img',
    'expected_sha256': 'fbcd67f55429c83550edd7c983b63f5862e7eccc8840de898b60aa6c62afdfd0'}
record['source_checkpoint'] = 'out/checkpoints/a733-v186-camera-start-20261027'
record['qualification'] = 'CPU fixtures and AArch64 objects ONLY; no live sensor, video, DMA or NPU run'
for item in record['files']:
    item.pop('command', None)
a.destination.mkdir()  # Never overwrite historical evidence.
for name in record['logs']:
    if name.startswith('test-') or name == 'build-wrapper-syntax.log':
        shutil.copyfile(a.check / name, a.destination / name)
(a.destination / 'summary.json').write_text(json.dumps(record, indent=2) + '\n')
print('RECORDED_VERIFIED_SOURCE_ONLY_CAMERA_EVIDENCE', a.destination)
