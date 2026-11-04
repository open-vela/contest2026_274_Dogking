#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Export a PRIVATE, provenance-preserving IMX214 config from pinned GPL BSP.

No download, firmware linking, MMIO or licence grant. The GPL-derived mode
asset and corresponding source stay in out/, not in the Apache source overlay.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import zlib

PINNED = 'e05a71fe4843c81d354ab9fa30bbd809796c21d557a87af54b550ba4e4a23832'


def table(text, name):
    match = re.search(r'static\s+struct\s+regval_list\s+' + re.escape(name) +
                      r'\[\]\s*=\s*\{(.*?)\n\};', text, re.S)
    if not match:
        raise ValueError('Missing exact table: ' + name)
    body = re.sub(r'/\*.*?\*/|//[^\n]*', '', match[1], flags=re.S)
    pairs = []
    pattern = r'\{\s*(0[xX][0-9a-fA-F]+|[0-9]+)\s*,\s*(0[xX][0-9a-fA-F]+|[0-9]+)\s*\}\s*,?'
    for item in re.finditer(pattern, body):
        reg, value = (int(n, 0) for n in item.groups())
        if not 0 <= reg <= 65535 or not 0 <= value <= 255:
            raise ValueError('Register/value out of range')
        pairs.append((reg, value))
    if re.sub(pattern, '', body).strip() or not pairs:
        raise ValueError('Nonliteral/unparsed entry: ' + name)
    return pairs


def export(source, output):
    data = source.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != PINNED:
        raise ValueError('Reference changed; independently re-audit before adding another hash')
    text = data.decode('utf-8')
    if 'SPDX-License-Identifier: GPL-2.0-or-later' not in text[:200]:
        raise ValueError('Source licence identifier absent')
    common = table(text, 'sensor_default_regs')
    mode = table(text, 'sensor_1080p_regs')
    if mode[-1] != (0x0100, 1) or any(r == 0x0100 for r, _ in mode[:-1] + common):
        raise ValueError('Unexpected stream sequence; do not silently rewrite it')
    # This explicit removal is recorded, not hidden: receiver MUST be armed
    # by the new runtime before the separate STREAM_ON operation.
    mode = mode[:-1]
    clock_facts = [(0x0136, 24), (0x0137, 0)]
    pairs = common + clock_facts + mode
    if any(r <= 0x0100 or r in (0x0103, 0xffff) for r, _ in pairs):
        raise ValueError('Reset/stream/identity write in configuration')
    effective = dict(pairs)
    facts = {0x0101:3,0x0112:10,0x0113:10,0x0114:3,0x0136:24,0x0137:0,
             0x0340:8,0x0341:0x50,0x0342:0x13,0x0343:0x90,
             0x034c:7,0x034d:0x80,0x034e:4,0x034f:0x38}
    if any(effective.get(r) != v for r, v in facts.items()):
        raise ValueError('Effective mode does not match the qualified source profile')
    if not 1 <= len(pairs) <= 512:
        raise ValueError('Operation bound')
    payload = b''.join(struct.pack('<BBH', 1, v, r) for r, v in pairs)
    header = bytearray(48)
    struct.pack_into('<4sHHIIIHHHHHBBBBBBII', header, 0,
        b'A7CM',1,48,48+len(payload),len(pairs),zlib.crc32(payload),
        0x0214,1920,1080,5008,2128,24,4,10,3,0,0,800,0)
    struct.pack_into('<I', header, 44, zlib.crc32(header[:44]))
    artifact = bytes(header) + payload
    output.mkdir()  # No replacement of a previous package.
    (output / 'imx214-1080p.a7cm').write_bytes(artifact)
    (output / 'imx214-reference.c').write_bytes(data)
    (output / 'SOURCE-LICENSE.txt').write_text(
        'Reference and derived configuration: GPL-2.0-or-later.\n'
        'Original copyright/licence notice and complete reference are in imx214-reference.c.\n'
        'Not relicensed as Apache-2.0. No firmware distribution/licence clearance asserted.\n'
        'Private development artifact only; review redistribution obligations before shipping.\n')
    manifest = dict(profile='allwinner-vin-imx214-1080p-24m-4lane-raw10-v1',
        source=str(source),source_sha256=digest,source_spdx='GPL-2.0-or-later',
        artifact_sha256=hashlib.sha256(artifact).hexdigest(),bytes=len(artifact),
        operations=len(pairs),payload_crc32=f'{zlib.crc32(payload):08x}',
        omitted_stream_on={'register':'0x0100','value':1,'entries':1},
        added_clock_facts={'0x0136':24,'0x0137':0},
        width=1920,height=1080,line_length=5008,frame_length=2128,
        lanes=4,wire_bits=10,lane_mbps=800,orientation=3,
        vendor_reported_bayer='SBGGR10',memory_layout='UNQUALIFIED',
        artifact_bayer_hint='UNKNOWN_until_real_frame_validation',
        sensor_execution=False,camera_capture=False,hardware_verified=False,
        firmware_distribution_cleared=False)
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    if not args.output.resolve().is_relative_to(root / 'out'):
        raise ValueError('Private vendor artifacts must stay under project out/')
    print(json.dumps(export(args.source.resolve(), args.output.resolve()), indent=2))
