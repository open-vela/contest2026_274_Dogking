#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Guarded SWOUT1 startup, with the byte-identical sealed v199 kernel.

Host generation only. No device execution, rail-voltage writes, saveenv or
media writes. The one permitted PMIC control write was tested by the user.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

REPO = Path(__file__).resolve().parents[1]
KERNEL_SHA = '19e2e0772816a44ee9ec671c3eac92f7978b2587a8a8c502c7abd60919cf96c0'
ELF_SHA = 'a0b3c9021c08139e9bd715bf09e1398a04822fb9271c7fe46a8fa4f3c4b0d243'
FIRMWARE_COMMIT = '6d1ece1c1a385db8f1f6163b172fbbf51a99a002'
MODE_NAMES = ('imx214-1080p.a7cm', 'imx214-reference.c', 'SOURCE-LICENSE.txt')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def legacy_module():
    spec = importlib.util.spec_from_file_location('camera199', REPO/'tools/build-a733-camera-handoff-v192.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def guarded_source(original, record):
    """Reuse the CRC scratch AFTER checking CRC; reserved bytes stay zero."""
    if original.count('camera-handoff-v199: snapshot committed') != 1:
        raise ValueError('Unexpected base script')
    source = original.replace('v199', 'v200')
    commit = f'mw.l {record+8:x} 434d5031 1'
    if source.count(commit) != 1:
        raise ValueError('Ambiguous ready commit')
    # 0x12 is DCDC1 voltage; 0x11 bit3 controls SWOUT1. Only the observed
    # 17->1f transition is allowed; 1f is an idempotent no-write path.
    # The before/after/voltage bytes occupy crc_scratch, not reserved[42].
    before, after, voltage = record+12, record+13, record+14
    gates = [
        "echo 'swout1-v200: checking DCDC enable10/voltage12'",
        f'md.b {record+18:x} 2',
        f'itest.b *{record+18:x} == ff',
        f'itest.b *{record+19:x} == 17',
        f'i2c read 36 11.1 1 {before:x}',
        "echo 'swout1-v200: control11 before (only 17 or1f)'",
        f'md.b {before:x} 1',
        f'if itest.b *{before:x} == 17; then sunxi_axp pmu write 11 1f; '
        f'elif itest.b *{before:x} == 1f; then true; else false; fi',
        'sleep 1',
        'i2c dev 0',
        f'i2c read 36 11.1 1 {after:x}',
        "echo 'swout1-v200: control11 readback (requires1f)'",
        f'md.b {after:x} 1',
        f'itest.b *{after:x} == 1f',
        f'i2c read 36 12.1 1 {voltage:x}',
        "echo 'swout1-v200: voltage12 readback (requires17)'",
        f'md.b {voltage:x} 1',
        f'itest.b *{voltage:x} == 17',
        commit,
    ]
    source = source.replace(commit, ' &&\n   '.join(gates))
    source = source.replace(
        f'  md.b {record+16:x} 6\n',
        f'  echo swout1-v200: VERIFIED; control-before/control-after/DCDC1-config\n'
        f'  md.b {before:x} 3 &&\n  md.b {record+16:x} 6 &&\n')
    # Quote the semicolon: it is a message, not a second shell command.
    source = source.replace(
        'echo swout1-v200: VERIFIED; control-before/control-after/DCDC1-config',
        "echo 'swout1-v200: VERIFIED; control-before/control-after/DCDC1-config'")
    source = source.replace(
        "  echo 'camera-handoff-v200: snapshot committed; rails retention is ASSUMED'\n",
        "  echo 'camera-handoff-v200: committed; SWOUT1 readback passed; runtime retention still ASSUMED' &&\n")
    source = source.replace(
        "FAILED; no kernel boot, no voltage/media writes",
        "FAILED; kernel NOT entered by this script; no voltage/media writes; SWOUT1 may remain on")
    return source


def legacy_image(source):
    image = legacy_module().legacy_script(source, 199)
    header = image[:32] + b'A733 SWOUT1 camera v200'.ljust(32, b'\0')
    header = header[:4] + bytes(4) + header[8:]
    return header[:4] + struct.pack('>I', zlib.crc32(header)) + header[8:] + image[64:]


def build(seal, destination):
    if destination.exists() or destination.is_symlink():
        raise ValueError('NEW destination required')
    if sha(seal/'nuttx.bin') != KERNEL_SHA or sha(seal/'nuttx') != ELF_SHA:
        raise ValueError('Only the sealed v199 firmware is qualified')
    with tempfile.TemporaryDirectory(prefix='a733-v200-source-') as temp:
        base = Path(temp)/'base'
        subprocess.run([sys.executable, str(REPO/'tools/build-a733-camera-handoff-v192.py'),
                        str(seal), str(base), '--release', '199', '--mode-directory', str(seal)],
                       check=True, capture_output=True, timeout=60)
        manifest = json.loads((base/'manifest.json').read_text())
        source = guarded_source((base/'camera-v199.cmd').read_text(),
                                int(manifest['handoff_loaded_address'], 16))
        script = legacy_image(source)
        destination.mkdir(parents=True)
        (destination/'camera-v200.cmd').write_text(source, newline='\n')
        (destination/'camera-v200.scr').write_bytes(script)
        for name in MODE_NAMES:
            (destination/name).write_bytes((base/name).read_bytes())
        manifest.update(result='V200_SWOUT1_STARTUP_GENERATED_NOT_BOARD_TESTED',
                        release=200, kernel_release=199, firmware_source_commit=FIRMWARE_COMMIT,
                        pmic_writes=True, pmic_voltage_writes=False,
                        allowed_control_write=dict(register='0x11', before='0x17', after='0x1f', changed_mask='0x08'),
                        already_enabled='0x1f: readback only, NO write',
                        required_dcdc1_enable='0xff (observed qualified profile)',
                        required_dcdc1_voltage_config='0x17 (3.3V configuration, NOT physical measurement)',
                        settle_seconds=1, runtime_pmic_access=False, auto_rollback=False,
                        scratch='3 bytes of verified crc_scratch; reserved[42] remains zero',
                        script_sha256=hashlib.sha256(script).hexdigest(),
                        script_source_sha256=hashlib.sha256(source.encode('ascii')).hexdigest(),
                        kernel_unchanged=True, software_guard_board_tested=False)
        (destination/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n', newline='\n')
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('seal', type=Path)
    parser.add_argument('new_directory', type=Path)
    args = parser.parse_args()
    print(json.dumps(build(args.seal, args.new_directory), indent=2))
