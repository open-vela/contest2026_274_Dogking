#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""New private image only; preserves the selected base outside its kernel partition."""
import argparse
import errno
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
BASE_SHA = 'fbcd67f55429c83550edd7c983b63f5862e7eccc8840de898b60aa6c62afdfd0'
ROOT_BEGIN, ROOT_END = 679936*512, 917504*512


def digest(path, begin=0, end=None):
    end = path.stat().st_size if end is None else end
    try:
        return linux_digest(path,begin,end)
    except OSError as error:
        # WSL 9p can return ENOMEM even for a bounded 64 KiB read. Recompute
        # the ENTIRE requested span through the native read-only filesystem;
        # never skip verification or reuse a partial digest after an I/O error.
        if error.errno!=errno.ENOMEM or not str(path.resolve()).startswith('/mnt/') or not shutil.which('powershell.exe'):
            raise
        def win(p):return subprocess.check_output(['wslpath','-w',str(p.resolve())],text=True).strip()
        print('WSL ENOMEM: recomputing full SHA256 span with native Windows I/O',path,begin,end,file=sys.stderr)
        value=subprocess.check_output(['powershell.exe','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass',
            '-File',win(REPO/'tools/Get-A733FileSpanDigest.ps1'),'-Path',win(path),
            '-Begin',str(begin),'-End',str(end)],timeout=180).decode('ascii').strip()
        assert re.fullmatch('[0-9a-f]{64}',value),'Invalid native digest'
        return value


def linux_digest(path, begin=0, end=None):
    result = hashlib.sha256()
    end = path.stat().st_size if end is None else end
    with path.open('rb') as stream:
        stream.seek(begin)
        remaining = end-begin
        while remaining:
            data = stream.read(min(65536, remaining))
            assert data, 'short image'
            result.update(data)
            remaining -= len(data)
    return result.hexdigest()


def configuration_profile(config, purpose, base_version):
    """Pure profile validation; camera opt-in cannot loosen voice checks."""
    required = ['CONFIG_A733_BOOT_DIAGNOSTICS=y', 'CONFIG_SMP_NCPUS=8',
                'CONFIG_BOARD_LATE_INITIALIZE=y', 'CONFIG_NSH_ARCHINIT=y',
                'CONFIG_BOARD_INITTHREAD_STACKSIZE=262144',
                'CONFIG_BOARD_INITTHREAD_PRIORITY=100', 'CONFIG_LIBC_MAX_EXITFUNS=0',
                'CONFIG_A733_CPU_ACOUSTIC=y', 'CONFIG_A733_NPU_GENERAL_ASR=y']
    bases = {'boot': ('185', '188', '189', '190'),
             'tts-rebuffer': ('188',), 'voice-pipeline': ('189',),
             'camera-qualification': ('190',), 'camera-control': ('191',),
             'camera-id': ('191',), 'camera-mode': ('191',), 'camera-packets': ('191',), 'camera-register-fix': ('191',), 'camera-frame': ('191',), 'camera-vision': ('191',), 'camera-sizing-fix': ('191',)}
    if purpose not in bases or base_version not in bases[purpose]:
        raise ValueError('Unsupported purpose/base profile')
    if purpose in ('camera-qualification', 'camera-control', 'camera-id', 'camera-mode','camera-packets','camera-register-fix','camera-frame','camera-vision','camera-sizing-fix'):
        required += ['CONFIG_A733_IMX214_CHECKPOINT=y', 'CONFIG_SYSTEM_A733CAMERA=y']
    elif 'CONFIG_A733_IMX214_CHECKPOINT=y' in config or 'CONFIG_SYSTEM_A733CAMERA=y' in config:
        raise ValueError('Camera enabled in a non-camera release')
    if purpose in ('camera-control', 'camera-id', 'camera-mode','camera-packets','camera-register-fix','camera-frame','camera-vision','camera-sizing-fix'):
        required += ['CONFIG_A733_CAMERA_CONTROL_TEST=y']
    elif 'CONFIG_A733_CAMERA_CONTROL_TEST=y' in config:
        raise ValueError('Active control enabled outside its exact release profile')
    if purpose in ('camera-vision','camera-sizing-fix'):required+=['CONFIG_SYSTEM_A733NPU=y','CONFIG_SYSTEM_A733DISPLAY=y','CONFIG_MQ_MAXMSGSIZE=32']
    missing = [line for line in required if line not in config]
    if missing:
        raise ValueError('Missing release configuration: '+', '.join(missing))
    return required


def main():
    if not __debug__:
        raise RuntimeError('Image packager requires Python assertion checks; do not use -O')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build_report', type=Path)
    parser.add_argument('sdk_build', type=Path)
    parser.add_argument('new_image', type=Path)
    parser.add_argument('new_evidence', type=Path)
    parser.add_argument('--base-version', choices=('185', '188', '189', '190', '191'), default='185')
    parser.add_argument('--purpose', choices=('boot', 'tts-rebuffer', 'voice-pipeline', 'camera-qualification', 'camera-control', 'camera-id', 'camera-mode','camera-packets','camera-register-fix','camera-frame','camera-vision','camera-sizing-fix'), default='boot')
    parser.add_argument('--camera-handoff', type=Path)
    parser.add_argument('--verify-existing-image',action='store_true',
                        help='Read-only revalidation of an existing candidate into a NEW evidence directory; never repack/overwrite')
    args = parser.parse_args()
    bases = {
        '185': ('openvela-a733-cubie-a7z-sd-v185-general-voice-candidate.img', BASE_SHA),
        '188': ('openvela-a733-cubie-a7z-sd-v188-boot-candidate.img',
                'a18142fda0ed79896af9a357a190034668089368809f255cac841085cc41c44b'),
        '189': ('openvela-a733-cubie-a7z-sd-v189-tts-rebuffer-candidate.img',
                'c56e2b4c42bd921a9edd8a69a91e05f39000de5c2d8aea50465b5ef66ee0c463'),
        '190': ('openvela-a733-cubie-a7z-sd-v190-offline-voice-candidate.img',
                '6082caf2fd059d06d1057abad1cf2bb726dbb4ab776f85c8a4f320c0b00f1ccb'),
        '191': ('openvela-a733-cubie-a7z-sd-v191-camera-qualification-candidate.img',
                '0de22bb8f79f34fc5438b2dec1830701156f855195ffca36125d63083e38b265')}
    base_name, base_sha = bases[args.base_version]
    base = REPO.parent/base_name
    image, evidence = args.new_image.resolve(), args.new_evidence.resolve()
    assert image.is_relative_to(REPO.parent) and image.suffix == '.img'
    assert evidence.is_relative_to(REPO/'out')
    assert not evidence.exists()
    if args.verify_existing_image:
        assert image.is_file() and not image.is_symlink() and image!=base.resolve()
    else:
        assert not image.exists()
    assert base.stat().st_size == 2147483648 and digest(base) == base_sha
    report, build = args.build_report.resolve(), args.sdk_build.resolve()
    assert (report/'wrapper-status.txt').read_text().strip() == '0'
    restored = json.loads((report/'sdk-restored.json').read_text())
    assert restored['result'] == 'SDK_RESTORED_BYTE_EXACT'
    config = (build/'.config').read_text().splitlines()
    required = configuration_profile(config, args.purpose, args.base_version)
    camera = args.purpose in ('camera-qualification', 'camera-control', 'camera-id', 'camera-mode','camera-packets','camera-register-fix','camera-frame','camera-vision','camera-sizing-fix')
    control = args.purpose in ('camera-control', 'camera-id', 'camera-mode','camera-packets','camera-register-fix','camera-frame','camera-vision','camera-sizing-fix')
    release = 199 if args.purpose=='camera-sizing-fix' else 198 if args.purpose=='camera-vision' else 197 if args.purpose=='camera-frame' else 196 if args.purpose=='camera-register-fix' else 195 if args.purpose=='camera-packets' else 194 if args.purpose=='camera-mode' else 193 if args.purpose == 'camera-id' else 192
    assert bool(args.camera_handoff) == control, 'Handoff assets only in the control profile'
    kernel = build/'nuttx.bin'
    assert kernel.stat().st_size > 6000000
    if camera:
        for marker in (b'A733 IMX214 v191', b'camera-selftest-v191:',
                       b'physical provider=unbound', b'offline-v190:'):
            assert marker in kernel.read_bytes(), marker
    if control:
        assert f'camera-control-v{release}:'.encode() in kernel.read_bytes()
    if args.purpose == 'tts-rebuffer':
        assert args.base_version == '188', 'TTS candidate requires boot-tested v188 base'
        assert b'local-tts-buffer-v189:' in kernel.read_bytes()
    if args.purpose == 'voice-pipeline':
        assert args.base_version == '189'
        for marker in (b'offline-v190:', b'voice-session-v190:', b'asr-compute-v190:',
                       b'local-tts-buffer-v189:'):
            assert marker in kernel.read_bytes(), marker
    objdump = build.parent.parent/'prebuilts/gcc/linux-x86_64/aarch64-none-elf/bin/aarch64-none-elf-objdump'
    disassembly = {name: subprocess.check_output(
        [str(objdump), '-d', '--disassemble='+name, str(build/'nuttx')], text=True)
        for name in ('nx_bringup', 'nx_start_task', 'lib_cxx_initialize', 'boot_stage_puts')}
    assert '<nxthread_create>' in disassembly['nx_bringup']
    assert '0x40000' in disassembly['nx_bringup'], 'missing 256 KiB initializer stack'
    assert '<lib_cxx_initialize>' not in disassembly['nx_bringup']
    assert '<lib_cxx_initialize>' in disassembly['nx_start_task']
    assert disassembly['lib_cxx_initialize'].count('<a733_boot_value>') == 2
    assert 'mpidr_el1' in disassembly['boot_stage_puts']
    assert '#8, #8' in disassembly['boot_stage_puts']
    assert 'cbz' in disassembly['boot_stage_puts']
    evidence.mkdir(parents=True)
    handoff = None
    if control:
        # Independent recomputation against final ELF/Image, not trusting an
        # asset manifest or a hard-coded loaded-record address supplied by CLI.
        generated = evidence/'handoff-recomputed'
        generate=['python3', str(REPO/'tools/build-a733-camera-handoff-v192.py'),
                  str(build), str(generated),'--release',str(release)]
        if release>=194:generate+=['--mode-directory',str(args.camera_handoff)]
        subprocess.run(generate, check=True)
        names=[f'camera-v{release}.cmd', f'camera-v{release}.scr', 'manifest.json']
        if release>=194:names+=['imx214-1080p.a7cm','imx214-reference.c','SOURCE-LICENSE.txt']
        for name in names:
            assert (generated/name).read_bytes() == (args.camera_handoff/name).read_bytes()
        handoff = json.loads((generated/'manifest.json').read_text())
        assert handoff['kernel_sha256'] == digest(kernel)
    (evidence/'boot-disassembly.txt').write_text('\n'.join(disassembly.values()))
    with (evidence/'package.log').open('x') as log:
        command = ['bash', str(REPO/'tools/package-a733-kernel-image.sh'),
                   str(base), str(image), str(kernel)]
        if control:
            command.append(str(generated.resolve()))
            command.append(str(release))
        if not args.verify_existing_image:
            subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
        elif control:
            subprocess.run(['bash',str(REPO/'tools/verify-a733-camera-assets.sh'),
                            str(image),str(generated),str(release)],
                           stdout=log,stderr=subprocess.STDOUT,check=True)
        subprocess.run(['bash', str(REPO/'tools/verify-a733-image.sh'),
                        str(image), digest(kernel)], stdout=log,
                       stderr=subprocess.STDOUT, check=True)
    assert image.stat().st_size == base.stat().st_size
    spans = []
    for begin, end in ((0, ROOT_BEGIN), (ROOT_END, base.stat().st_size)):
        expected = digest(base, begin, end)
        assert digest(image, begin, end) == expected
        spans.append(dict(begin=begin, end=end, sha256=expected))
    result = dict(result='PRIVATE_CANDIDATE_PACKAGED_NOT_BOARD_TESTED',
                  purpose=args.purpose,
                  image=str(image), bytes=image.stat().st_size, sha256=digest(image),
                  base_sha256=base_sha, kernel_sha256=digest(kernel),
                  packaging_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'],
                                                           cwd=REPO, text=True).strip(),
                  required_config=required, sdk_restore=restored,
                  unchanged_image_spans=spans, model_fat_unchanged=True,
                  compiled_boot_call_graph_checked=True,
                  elf_sha256=digest(build/'nuttx'),
                  config_sha256=digest(build/'.config'),
                  hardware_executed=False, root_cause_confirmed=False,
                  camera_qualification_candidate=camera,
                  camera_reset_held_control_candidate=args.purpose=='camera-control',
                  camera_id_experiment_candidate=args.purpose=='camera-id',
                  camera_mode_experiment_candidate=args.purpose=='camera-mode',
                  camera_packet_experiment_candidate=args.purpose in ('camera-packets','camera-register-fix'),
                  camera_register_fix_candidate=args.purpose=='camera-register-fix',
                  camera_single_frame_candidate=args.purpose in ('camera-frame','camera-vision','camera-sizing-fix'),
                  camera_same_input_vision_candidate=args.purpose in ('camera-vision','camera-sizing-fix'),
                  camera_auto_sizing_repair_candidate=args.purpose=='camera-sizing-fix',
                  camera_handoff=handoff,
                  physical_camera_provider_bound=False, camera_capture=False,
                  physical_disks_written=False, old_images_modified=False,
                  existing_candidate_revalidated=args.verify_existing_image,
                  models_private=True)
    (evidence/'summary.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
