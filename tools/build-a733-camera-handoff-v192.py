#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build an exact-Image-bound, RAM-only U-Boot PMIC handoff script.

No device execution or flashing. Numeric addresses below are the user's
observed U-Boot environment, NOT generic A733 defaults. Record addresses
are derived from the exact ELF and adjusted for booti's source relocation.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import struct
import zlib

SIGNATURE, ABI, READY = 0x41375057, 0x00019201, 0x434d5031
LOAD, FDT, SCRIPT = 0x40080000, 0x4fa00000, 0x4fc00000
REGS = (0x0e, 0x0f, 0x10, 0x12, 0x20, 0x2a)


def image_script(record_link, image_start, image_bytes, runtime_bytes, kernel_crc,
                 release=192,mode_record_link=None):
    """Return deterministic Hush source. Failure paths never reach booti."""
    if release not in (192,193,194,195,196,197,198,199):
        raise ValueError('Unsupported camera release')
    if image_start != 0x40200000 or record_link % 64:
        raise ValueError('Unqualified linked Image/record layout')
    offset = record_link-image_start
    if not 64 <= offset <= image_bytes-64:
        raise ValueError('Handoff record must be within the file-backed Image')
    if not image_bytes <= runtime_bytes <= 0x48000000-image_start:
        raise ValueError('Image/BL31 footprint')
    if LOAD+runtime_bytes >= FDT or FDT+0x100000 >= SCRIPT:
        raise ValueError('Boot payload overlap')
    if kernel_crc in (0, 0xffffffff):
        raise ValueError('Refuse ambiguous all-zero/all-one CRC sentinel')
    record = LOAD+offset
    mode_record=None
    if release>=194:
        if mode_record_link is None or mode_record_link%64:
            raise ValueError('Mode handoff layout missing/unaligned')
        mo=mode_record_link-image_start
        if not 64<=mo<=image_bytes-704 or not (mo+704<=offset or offset+64<=mo):
            raise ValueError('Mode/power record overlap or non-file-backed mode')
        mode_record=LOAD+mo
    scratch = record+12
    swapped = int.from_bytes(kernel_crc.to_bytes(4,'little'),'big')
    checks = [
        f'ext4load mmc 0:3 {LOAD:x} /boot/openvela/a733/Image',
        f'itest.l ${{filesize}} == {image_bytes:x}',
        # Existing U-Boot variants store crc32 as a native word OR digest
        # bytes. Compare both byte orders; this is error detection, not auth.
        f'crc32 {LOAD:x} {image_bytes:x} {scratch:x}',
        f'if itest.l *{scratch:x} == {kernel_crc:x} || itest.l *{scratch:x} == {swapped:x}; then true; else false; fi',
        f'itest.l *{record:x} == {SIGNATURE:x}',
        f'itest.l *{record+4:x} == {ABI:x}',
        f'mw.l {record+8:x} 0 1',
    ]
    if mode_record is not None:
        checks += [f'itest.l *{mode_record:x} == 41374d44',
                   f'itest.l *{mode_record+4:x} == 1',
                   f'ext4load mmc 0:3 {mode_record+16:x} /boot/openvela/a733/imx214-1080p.a7cm 294',
                   'itest.l ${filesize} == 294',
                   f'mw.l {mode_record+8:x} 294 1',
                   f'mw.l {mode_record+12:x} 434d4f31 1']
    checks += ['i2c dev 0']
    checks += [f'i2c read 36 {reg:02x}.1 1 {record+16+i:x}' for i,reg in enumerate(REGS)]
    checks += [f'itest.b *{record+16:x} == 3', f'itest.b *{record+17:x} == 0',
               f'ext4load mmc 0:3 {FDT:x} /boot/dtb/sun60i-a733-cubie-a7z.dtb',
               f'mw.l {record+8:x} {READY:x} 1']
    # A single guarded AND chain is accepted by both Hush and the host
    # command-model test. No setenv/saveenv or block/media write commands.
    source = '# v192 exact-Image RAM handoff; NOT a PMIC voltage write or camera stream\n'
    source += 'echo camera-handoff-v192: loading exact Image and reading PMIC\n'
    source += 'if '+ ' &&\n   '.join(checks) + '; then\n'
    source += f'  md.b {record+16:x} 6\n'
    source += "  echo 'camera-handoff-v192: snapshot committed; rails retention is ASSUMED'\n"
    source += f'  booti {LOAD:x} - {FDT:x}\n'
    source += "else\n  echo 'camera-handoff-v192: FAILED; no kernel boot, no voltage/media writes'\n  false\nfi\n"
    return source.replace('v192',f'v{release}'), record


def legacy_script(source, release=192):
    if release not in (192,193,194,195,196,197,198,199):
        raise ValueError('Unsupported camera release')
    data=source.encode('ascii')
    payload=struct.pack('>II',len(data),0)+data
    name=f'A733 camera handoff v{release}'.encode('ascii').ljust(32,b'\0')
    # Legacy uImage: magic,hcrc,time,size,load,entry,dcrc,OS,ARCH,TYPE,COMP,name.
    header=struct.pack('>7I4B32s',0x27051956,0,0,len(payload),0,0,
                       zlib.crc32(payload),5,2,6,0,name)
    header=header[:4]+struct.pack('>I',zlib.crc32(header))+header[8:]
    return header+payload


def main():
    from elftools.elf.elffile import ELFFile
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('build',type=Path)
    p.add_argument('new_directory',type=Path)
    p.add_argument('--release',type=int,choices=(192,193,194,195,196,197,198,199),default=192)
    p.add_argument('--mode-directory',type=Path)
    a=p.parse_args()
    if bool(a.mode_directory)!=(a.release>=194):
        raise ValueError('External mode assets required for v194/v195/v196 only')
    binary=(a.build/'nuttx.bin').read_bytes()
    mode_link=None;mode_assets={}
    # pyelftools performs many small seeks for symbols/strings. Keep the
    # immutable ELF in RAM, as the boot ELF auditor does, rather than turn
    # each seek into a Windows/WSL 9p RPC. No firmware/protocol change.
    with io.BytesIO((a.build/'nuttx').read_bytes()) as stream:
        elf=ELFFile(stream)
        if elf.elfclass!=64 or not elf.little_endian or elf['e_machine']!='EM_AARCH64':
            raise ValueError('Wrong firmware ELF')
        symbols={s.name:s for s in elf.get_section_by_name('.symtab').iter_symbols() if s.name}
        start=symbols['_image_start']['st_value']
        runtime=symbols['_image_end']['st_value']-start
        symbol=symbols['g_a733_camera_power_handoff']
        link=symbol['st_value']
        if symbol['st_size']!=64:
            raise ValueError('Wrong handoff size')
        section=elf.get_section(symbol['st_shndx'])
        if not section.name.startswith('.data') or section['sh_type']!='SHT_PROGBITS':
            raise ValueError('Handoff must survive BSS initialization in file-backed .data')
        offset=link-start
        expected=struct.pack('<4I',SIGNATURE,ABI,0,0)+bytes(48)
        if binary[offset:offset+64]!=expected:
            raise ValueError('ELF-to-Image mapping or initial handoff mismatch')
        if a.release>=194:
            symbol=symbols['g_a733_camera_mode_handoff'];mode_link=symbol['st_value']
            section=elf.get_section(symbol['st_shndx'])
            if symbol['st_size']!=704 or not section.name.startswith('.data') or section['sh_type']!='SHT_PROGBITS':
                raise ValueError('Invalid mode handoff ELF section/size')
            expected=struct.pack('<4I',0x41374d44,1,0,0)+bytes(688)
            if binary[mode_link-start:mode_link-start+704]!=expected:
                raise ValueError('Mode handoff initial bytes mismatch')
    if a.release>=194:
        for name in ('imx214-1080p.a7cm','imx214-reference.c','SOURCE-LICENSE.txt'):
            path=a.mode_directory/name
            if path.is_symlink() or not path.is_file() or path.stat().st_size>1048576:
                raise ValueError('Invalid external mode asset')
            mode_assets[name]=path.read_bytes()
        if hashlib.sha256(mode_assets['imx214-1080p.a7cm']).hexdigest()!='a7b2102ba05c51de1a8a7602152505f4feedbeb3ec9facdda70710f9639bb2af':
            raise ValueError('Mode artifact changed')
        if hashlib.sha256(mode_assets['imx214-reference.c']).hexdigest()!='e05a71fe4843c81d354ab9fa30bbd809796c21d557a87af54b550ba4e4a23832':
            raise ValueError('Corresponding GPL source changed')
        if b'GPL-2.0-or-later' not in mode_assets['SOURCE-LICENSE.txt']:
            raise ValueError('GPL licence note missing')
    source,loaded=image_script(link,start,len(binary),runtime,zlib.crc32(binary),a.release,mode_link)
    a.new_directory.mkdir()  # Never overwrite a previous artifact.
    (a.new_directory/f'camera-v{a.release}.cmd').write_text(source,newline='\n')
    script=legacy_script(source,a.release)
    (a.new_directory/f'camera-v{a.release}.scr').write_bytes(script)
    for name,data in mode_assets.items():(a.new_directory/name).write_bytes(data)
    manifest=dict(result='RAM_HANDOFF_SCRIPT_BUILT_NOT_BOARD_TESTED',
        kernel_sha256=hashlib.sha256(binary).hexdigest(),kernel_crc32=f'{zlib.crc32(binary):08x}',
        kernel_bytes=len(binary),image_start=hex(start),runtime_bytes=runtime,
        handoff_link_address=hex(link),handoff_loaded_address=hex(loaded),
        handoff_values_address=hex(loaded+16),script_sha256=hashlib.sha256(script).hexdigest(),
        environment_observed=dict(kernel_addr_r=hex(LOAD),fdt_addr_r=hex(FDT),scriptaddr=hex(SCRIPT)),
        pmic_address='0x36',pmic_registers=[hex(r) for r in REGS],
        pmic_writes=False,media_writes=False,saveenv=False,
        runtime_rail_measurement=False,camera_capture=False,hardware_verified=False)
    if a.release>=194:
        manifest.update(mode_handoff_link_address=hex(mode_link),
            mode_handoff_loaded_address=hex(LOAD+mode_link-start),
            external_mode_files={name:hashlib.sha256(data).hexdigest() for name,data in mode_assets.items()},
            mode_static_linked=False,mode_stream_on=False)
    if a.release>=195:manifest['packet_experiment_available']=True
    if a.release>=197:manifest['single_frame_experiment_available']=True
    (a.new_directory/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',newline='\n')
    print(json.dumps(manifest,indent=2))


if __name__=='__main__':
    main()
