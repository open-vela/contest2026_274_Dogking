#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Archive verified v207 host/build/image lineage, never board acceptance."""
from pathlib import Path
import argparse, hashlib, io, json, shutil, struct, subprocess, zlib
from elftools.elf.elffile import ELFFile

repo = Path(__file__).resolve().parents[1]

def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        while block := stream.read(1048576):
            h.update(block)
    return h.hexdigest()

def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('check', 'package', 'build_report', 'seal', 'output'):
        p.add_argument('--' + name.replace('_', '-'), type=Path, required=True)
    p.add_argument('--windows-sha256', required=True)
    p.add_argument('--interrupted-build-report', type=Path)
    a = p.parse_args()
    checked = json.loads((a.check/'summary.json').read_text())
    package = json.loads((a.package/'summary.json').read_text())
    assert checked['result'] == 'V207_PERF_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED'
    assert package['result'] == 'V207_RAM_LIVE_IMAGE_PACKAGED_NOT_BOARD_TESTED'
    assert checked['firmware_commit'] == package['firmware_commit']
    assert not checked['hardware_verified'] and not package['hardware_verified']
    assert checked['npu_hardware_execution_unchanged'] and not checked['continuous_video_qualified']
    assert not package['physical_disks_written'] and not package['ufs_written']
    assert package['models_fat_unchanged']
    image = Path(package['image'])
    assert image.is_file() and image.stat().st_size == 2147483648
    assert sha(image) == package['sha256'] == a.windows_sha256.lower()
    assert Path(str(image)+'.sha256').read_text().strip() == package['sha256']+'  '+image.name
    assert (a.build_report/'wrapper-status.txt').read_text().strip() == '0'
    assert json.loads((a.build_report/'sdk-restored.json').read_text()) == checked['sdk_restore'] == dict(result='SDK_RESTORED_BYTE_EXACT', paths=295)
    assert sha(a.build_report/'build.log') == checked['build_log_sha256']
    for name, digest in checked['build_artifact_sha256'].items():
        assert sha(a.seal/name) == digest
    assert sha(a.seal/'nuttx.bin') == package['kernel_sha256']
    for name, digest in checked['firmware_source_sha256'].items():
        assert sha(repo/name) == digest == package['firmware_source_sha256'][name]
        committed=subprocess.check_output(['git','show',checked['firmware_commit']+':'+name],cwd=repo)
        if hashlib.sha256(committed).hexdigest()!=digest:
            assert name in checked['working_CRLF_only_files'] and name in package['working_CRLF_only_files']
            assert Path(name).suffix=='.json' and committed==(repo/name).read_bytes().replace(b'\r\n',b'\n')
    for name, digest in checked['tests'].items():
        assert sha(a.check/(name+'.log')) == digest
    native = json.loads((a.check/'native/summary.json').read_text())
    assert native['result'] == 'V205_NATIVE_CONFIG_ARM_OBJECTS_PASSED' and not native['hardware_verified']
    elf = ELFFile(io.BytesIO((a.seal/'nuttx').read_bytes()))
    symbols = elf.get_section_by_name('.symtab')
    for name, address in checked['symbols'].items():
        entry = symbols.get_symbol_by_name(name)
        assert entry and hex(entry[0]['st_value']) == address
    frame = symbols.get_symbol_by_name('frame_buffer')[0]
    assert frame['st_value'] % 4096 == 0 and frame['st_size'] == 4194304
    assert elf.get_section(frame['st_shndx'])['sh_type'] == 'SHT_NOBITS'
    manifest = json.loads((a.seal/'camera-v207-manifest.json').read_text())
    assert manifest == package['boot_assets'] and manifest['release'] == 207
    assert manifest['kernel_release']==207 and manifest['dma_rearm_resubmits_addresses']
    assert checked['voice_apps_and_tensor_crc_unchanged_from_v206']
    assert len(checked['tests'])==17 and manifest['raw_crc']=='portable-slicing8'
    assert manifest['fused_output_scan'] and manifest['post_run_weight_verification']
    assert not manifest['pre_run_duplicate_weight_scan']
    assert manifest['stream_stop_reports_cleanup_only'] and manifest['data_errors_require_reboot']
    assert manifest['pmic_writes'] and not manifest['pmic_voltage_writes'] and not manifest['runtime_pmic_access']
    assert manifest['live_max_seconds'] == 3600 and manifest['clean_cycle_budget'] == 32768
    assert manifest['continuous_sensor_stream'] and not manifest['continuous_dma'] and not manifest['mp4_recording']
    script = (a.seal/'camera-v207.scr').read_bytes()
    source = (a.seal/'camera-v207.cmd').read_bytes()
    assert script == (a.package/'assets/camera-v207.scr').read_bytes()
    assert source == (a.package/'assets/camera-v207.cmd').read_bytes()
    assert hashlib.sha256(script).hexdigest() == manifest['script_sha256']
    header = struct.unpack('>7I4B32s', script[:64])
    assert header[0] == 0x27051956 and header[9] == 6 and header[10] == 0
    assert zlib.crc32(script[:4]+bytes(4)+script[8:64]) == header[1]
    assert len(script[64:]) == header[3] and zlib.crc32(script[64:]) == header[6]
    assert script[72:] == source
    output = a.output.resolve()
    assert output.is_relative_to(repo/'docs/evidence') and not output.exists()
    output.mkdir(parents=True)
    copied = {}
    def copy(src, relative):
        dest = output/relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, dest)
        copied[str(relative)] = sha(dest)
    for base, prefix in ((a.check,'host'), (a.package,'package')):
        for src in sorted(base.rglob('*')):
            if src.is_file() and src.suffix in ('.json','.log','.cmd','.scr'):
                copy(src, Path(prefix)/src.relative_to(base))
    for name in ('wrapper-status.txt','sdk-restored.json','build.log'):
        copy(a.build_report/name, Path('build')/name)
    if a.interrupted_build_report:
        prior=a.interrupted_build_report
        assert (prior/'wrapper-status.txt').read_text().strip()=='2'
        assert json.loads((prior/'sdk-restored.json').read_text())==dict(result='SDK_RESTORED_BYTE_EXACT',paths=295)
        for name in ('wrapper-status.txt','sdk-restored.json','build.log'):
            copy(prior/name,Path('interrupted-r1')/name)
    result = dict(result='V207_SEALED_LINEAGE_AND_IMAGE_VERIFIED_NOT_BOARD_TESTED',
        image=image.name, bytes=image.stat().st_size, sha256=package['sha256'],
        windows_sha256=a.windows_sha256.lower(), firmware_commit=checked['firmware_commit'],
        package_commit=package['package_commit'], archive_sha256=copied,
        seal_sha256={f.name:sha(f) for f in a.seal.iterdir() if f.is_file()},
        hardware_verified=False, continuous_video_qualified=False,
        private_camera_images_archived=False)
    (output/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
    print(result['result'], len(copied), 'evidence files')

if __name__ == '__main__':
    main()
