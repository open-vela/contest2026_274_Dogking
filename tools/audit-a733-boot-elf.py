#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Read-only boot layout/constructor inventory of the EXACT shipped ELF.

Requires pyelftools. Does not execute the firmware or infer hardware success.
Prints JSON to stdout; no SDK, image, device, or source writes.
"""
import argparse
import hashlib
import io
import json
import struct
from pathlib import Path

from elftools.elf.elffile import ELFFile


def audit(path):
    raw = path.read_bytes()
    with io.BytesIO(raw) as stream:
        elf = ELFFile(stream)
        assert elf.elfclass == 64 and elf.little_endian
        assert elf['e_machine'] == 'EM_AARCH64'
        symbols = list(elf.get_section_by_name('.symtab').iter_symbols())
        named = {s.name: s for s in symbols if s.name}
        funcs = {s['st_value']: s.name for s in symbols
                 if s['st_info']['type'] == 'STT_FUNC' and s['st_value']}
        start, end = (named[n]['st_value'] for n in ('_sinit', '_einit'))
        assert start % 8 == 0 and end >= start and (end-start) % 8 == 0
        table = next(s for s in elf.iter_sections()
                     if s['sh_addr'] <= start and end <= s['sh_addr']+s['sh_size'])
        data = table.data()[start-table['sh_addr']:end-table['sh_addr']]
        ctors = [dict(index=i//8, address=hex(p), symbol=funcs.get(p, 'UNKNOWN'))
                 for i in range(0, len(data), 8)
                 for p in (struct.unpack_from('<Q', data, i)[0],)]
        layout = {n: hex(named[n]['st_value']) for n in (
            '_stext', '_etext', '_sdata', '_edata', '_sbss', '_ebss',
            '_s_initstack', '_e_initstack', 'g_cpu_idlestackalloc',
            'g_idle_topstack', '_image_file_end', '_image_end', 'g_nx_initstate')
            if n in named}
        large = sorted((s for s in symbols if s['st_info']['type'] == 'STT_OBJECT'),
                       key=lambda s: s['st_size'], reverse=True)[:20]
        return dict(result='READ_ONLY_ELF_INVENTORY_NOT_BOOT_PROOF', elf=str(path),
                    sha256=hashlib.sha256(raw).hexdigest(),
                    layout=layout, constructors=ctors,
                    tls_sections=[s.name for s in elf.iter_sections()
                                  if s['sh_flags'] & 0x400],
                    large_objects=[dict(name=s.name, address=hex(s['st_value']),
                                        bytes=s['st_size']) for s in large])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    args = parser.parse_args()
    print(json.dumps(audit(args.elf), indent=2))
