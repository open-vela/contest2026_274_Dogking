#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""A7Z Linux UFS evidence, read-only. No flash, mkfs, mount or device writes.

Default collects sysfs/DT/filtered logs only, WITHOUT reading raw block data.
Optional explicit --benchmark /dev/sdX reads at most 64 MiB through O_DIRECT;
only a whole LUN proven under 4520000.ufs/sunxi-ufs-pltfm is accepted.
Output is a new private directory and new archive; previous results preserved.
"""
import argparse
import hashlib
import itertools
import json
import mmap
import os
import platform
import re
import stat
import subprocess
import tarfile
import tempfile
import time
from pathlib import Path

BOUND = 1024 * 1024

# Provisional reference, NOT an assertion about the installed ordering suffix.
# Kingston CY14-02J01 v1.3, operating ranges; no PMIC programming values here.
CY14_UV = {'vcc': (2400000, 2700000), 'vccq': (1140000, 1260000)}


def small(path, limit=BOUND):
    try:
        with path.open('rb') as f:
            data = f.read(limit + 1)
        if len(data) > limit:
            return {'error': 'read bound exceeded'}
        return data.decode('utf-8', errors='replace').strip()
    except OSError as e:
        return {'error': type(e).__name__, 'errno': e.errno}


def ufs_blocks(root=Path('/')):
    result = {}
    for block in sorted((root/'sys/class/block').glob('*')):
        if (block/'partition').exists():
            continue  # benchmark a proven whole LUN, never a partition guessing
        device = (block/'device').resolve()
        controller = next((p for p in device.parents if p.name == '4520000.ufs'), None)
        if controller is None or (controller/'driver').resolve().name != 'sunxi-ufs-pltfm':
            continue
        result[block.name] = dict(sysfs=str(device), controller=str(controller),
            attributes={name: small(block/name) for name in [
                'dev', 'size', 'ro', 'queue/logical_block_size',
                'queue/physical_block_size', 'queue/max_sectors_kb',
                'device/vendor', 'device/model', 'device/rev', 'device/state',
                'device/queue_depth']})
    return result


def dt_bytes(path):
    """Bounded binary property, never reinterpret a voltage constraint as live."""
    try:
        with path.open('rb') as f:
            data = f.read(4097)
        return data if len(data) <= 4096 else None
    except OSError:
        return None


def dt_u32(path):
    value = dt_bytes(path)
    return int.from_bytes(value, 'big') if value is not None and len(value) == 4 else None


def inside(path, parent):
    try:
        return path.resolve(strict=True).relative_to(parent.resolve(strict=True))
    except (OSError, ValueError, RuntimeError):
        return None


def electrical_evidence(root=Path('/')):
    """Read linked DT + regulator sysfs only. Never enables cold start/DMA.

    Linux regulator microvolts is a configured/readback value, not an independent
    pin measurement. VCCQ2 in an old Samsung DT does NOT prove a third Kingston
    supply; CY14-02J01 marks those former balls NC. Missing/ambiguous evidence
    remains unknown. No descriptor queries or PMIC/MMIO writes are performed.
    """
    result = dict(target_family='Kingston UFS128-CY14 (user marking)',
        ordering_suffix_confirmed=False, installed_board_revision_confirmed=False,
        reference_profile='CY14-02J01-v1.3-provisional',
        cold_start_authorized=False, physical_voltage_verified=False,
        dma_mapping_verified=False, supplies={},
        note='Read-only preparation, not electrical acceptance or cold-start authorization.')
    dt = root/'sys/firmware/devicetree/base'
    ctrl = root/'sys/bus/platform/devices/4520000.ufs'
    try:
        result['controller_bound'] = (ctrl/'driver').resolve().name == 'sunxi-ufs-pltfm'
    except (OSError, RuntimeError):
        result['controller_bound'] = False
    node = ctrl/'of_node'
    relative = inside(node, dt)
    if relative is None:
        result['error'] = 'No controller-linked live DT node; no supply-name guessing'
        return result
    node = node.resolve(strict=True)
    result['dt_node'] = str(relative)
    result['iommu_group'] = ((ctrl/'iommu_group').resolve().name
                             if (ctrl/'iommu_group').exists() else None)
    result['iommus_property_hex'] = ((dt_bytes(node/'iommus') or b'').hex())
    result['iommu_note'] = 'Missing group/property does not prove DMA identity/bypass.'

    # Bound traversal even for malformed test trees. Live DT has no child links.
    phandles = {}
    count = 0
    for current, directories, files in os.walk(dt, followlinks=False):
        count += 1
        if count > 4096:
            result['error'] = 'DT traversal bound exceeded; no partial supply resolution'
            return result
        directories[:] = [d for d in directories if not (Path(current)/d).is_symlink()]
        handles = []
        for name in ('phandle', 'linux,phandle'):
            if name in files:
                value = dt_u32(Path(current)/name)
                handles.append(value)
        if handles and (any(v in (None, 0, 0xffffffff) for v in handles) or
                        len(set(handles)) != 1):
            result['error'] = 'Conflicting/malformed DT phandle; no supply resolution'
            return result
        if handles:
            phandles.setdefault(handles[0], set()).add(Path(current))
    regulators = sorted(itertools.islice((root/'sys/class/regulator').glob('regulator.*'), 257))
    if len(regulators) > 256:
        result['error'] = 'Regulator inventory bound exceeded'
        return result
    for supply in ('vcc', 'vccq', 'vccq2'):
        value = dt_u32(node/(supply+'-supply'))
        entry = dict(phandle=value, resolution='unknown', configured_range_match=None,
                     sysfs=[], physical_voltage_verified=False)
        result['supplies'][supply] = entry
        targets = phandles.get(value, set())
        if len(targets) != 1:
            entry['reason'] = 'Missing, malformed, unresolved or duplicate DT phandle'
            continue
        target = next(iter(targets))
        entry.update(resolution='unique-dt-node', node=str(target.relative_to(dt)),
                     name=small(target/'regulator-name', 256),
                     dt_min_microvolts=dt_u32(target/'regulator-min-microvolt'),
                     dt_max_microvolts=dt_u32(target/'regulator-max-microvolt'))
        for regulator in regulators:
            if inside(regulator/'of_node', dt) != target.relative_to(dt):
                continue
            entry['sysfs'].append(dict(node=regulator.name, attributes={key:
                small(regulator/key, 256) for key in
                ('name', 'microvolts', 'state', 'num_users', 'min_microvolts', 'max_microvolts')}))
        if supply == 'vccq2':
            entry['reason'] = 'Legacy DT name only; not a validated CY14 third supply. Audit NC-ball wiring.'
        elif len(entry['sysfs']) == 1:
            live = entry['sysfs'][0]['attributes']
            uv = live['microvolts']
            if isinstance(uv, str) and re.fullmatch(r'[0-9]{1,9}', uv):
                low, high = CY14_UV[supply]
                entry['reference_range_microvolts'] = [low, high]
                entry['configured_range_match'] = low <= int(uv) <= high
            else:
                entry['reason'] = 'No numeric live regulator readback'
        else:
            entry['reason'] = 'No unique regulator linked by of_node; names/ranges are not live evidence'
    return result


def benchmark(device, inventory):
    # No arbitrary paths, symlinks, partitions, eMMC, USB/SATA disks, retries.
    path = Path(device)
    if not re.fullmatch(r'/dev/sd[a-z]{1,3}', str(path)) or path.is_symlink():
        raise ValueError('Use an explicit, non-symlink whole UFS LUN /dev/sdX')
    item = inventory.get(path.name)
    if item is None:
        raise ValueError('Not proven under A733 UFS platform controller')
    info = path.stat()
    if not stat.S_ISBLK(info.st_mode):
        raise ValueError('Not a block device')
    major, minor = str(item['attributes']['dev']).split(':')
    if info.st_rdev != os.makedev(int(major), int(minor)):
        raise ValueError('sysfs/device identity mismatch')
    capacity = int(item['attributes']['size']) * 512
    offset, length, chunk = 32 * BOUND, 64 * BOUND, BOUND
    if capacity < offset + length:
        raise ValueError('LUN too small for fixed bounded read test')
    fd = os.open(path, os.O_RDONLY | os.O_CLOEXEC | os.O_NONBLOCK | os.O_DIRECT)
    try:
        if os.fstat(fd).st_rdev != info.st_rdev:
            raise ValueError('Device changed at open')
        crc = hashlib.sha256()
        with mmap.mmap(-1, chunk) as buf:
            start = time.monotonic()
            for pos in range(offset, offset + length, chunk):
                if time.monotonic() - start > 30:
                    raise TimeoutError('No further reads after 30s budget')
                actual = os.preadv(fd, [buf], pos)
                if actual != chunk:
                    raise OSError('Short O_DIRECT read; no retry')
                crc.update(buf)
            elapsed = time.monotonic() - start
        return dict(device=str(path), direct_io=True, bytes=length,
                    offset=offset, seconds=elapsed, MiB_per_second=64/elapsed,
                    sha256=crc.hexdigest(), raw_device_writes=False,
                    note='Read+hash wall time, not pure device throughput. A kernel read can exceed userspace budget; no global cache flush.')
    finally:
        os.close(fd)


def collect(root=Path('/')):
    result = dict(schema=2, board='A733 Cubie A7Z (user target; confirm DT model)',
                  kernel=platform.release(), blocks=ufs_blocks(root),
                  raw_device_reads=False, raw_device_reads_attempted=False,
                  raw_device_writes=False, report_files_written=True,
                  complete_ufs_driver_or_speed_acceptance=False)
    result['electrical_evidence'] = electrical_evidence(root)
    result['model'] = small(root/'sys/firmware/devicetree/base/model')
    result['compatible'] = small(root/'sys/firmware/devicetree/base/compatible')
    result['device_tree'] = {}
    dt = root/'sys/firmware/devicetree/base'
    for node in dt.glob('**/*ufs*'):
        if not node.is_dir():
            continue
        props = {}
        for key in ['compatible', 'status', 'reg', 'interrupts', 'clocks', 'clock-names',
                    'resets', 'reset-names', 'vcc-supply', 'vccq-supply', 'vccq2-supply']:
            file = node/key
            try:
                with file.open('rb') as f:
                    data = f.read(4097)
                props[key] = dict(bytes=len(data), hex=data.hex()) if len(data) <= 4096 else {'error': 'bound'}
            except OSError as e:
                props[key] = {'error': e.errno}
        result['device_tree'][str(node.relative_to(dt))] = props
    # No full environment, network credentials, command line, user data/model
    # files, host keys or arbitrary dmesg in this report.
    for name, pattern in [('clk/clk_summary', r'ufs|store|msi.lite|peri0|dcxo'),
                          ('regulator/regulator_summary', r'dldo6|dcdc8|ufs')]:
        value = small(root/'sys/kernel/debug'/name)
        result[name] = ([line for line in value.splitlines() if re.search(pattern, line, re.I)]
                        if isinstance(value, str) else value)
    return result


def save(result, parent):
    # Parent must exist; create a unique sibling instead of overwriting.
    folder = Path(tempfile.mkdtemp(prefix='a733-ufs-linux-', dir=parent))
    os.chmod(folder, 0o700)
    data = (json.dumps(result, indent=2, ensure_ascii=True)+'\n').encode()
    with (folder/'report.json').open('xb') as f:
        f.write(data)
    with (folder/'SHA256SUMS').open('x') as f:
        f.write(hashlib.sha256(data).hexdigest()+'  report.json\n')
    archive = folder.with_suffix('.tar.gz')
    with archive.open('xb') as target:
        os.chmod(archive, 0o600)
        with tarfile.open(fileobj=target, mode='w:gz') as tar:
            for name in ['report.json', 'SHA256SUMS']:
                tar.add(folder/name, arcname=folder.name+'/'+name, recursive=False)
    return archive


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output-parent', type=Path, default=Path.cwd())
    p.add_argument('--benchmark', metavar='/dev/sdX', help='Optional explicit 64 MiB O_DIRECT read, no writes')
    args = p.parse_args()
    if platform.system() != 'Linux':
        p.exit(2, 'Run on A7Z official Linux, not Windows/WSL for hardware evidence.\n')
    report = collect()
    try:
        dmesg = subprocess.run(['dmesg', '--color=never'], capture_output=True,
                               timeout=10, check=False)
        if len(dmesg.stdout) > 8 * BOUND:
            raise ValueError('dmesg bound exceeded')
        report['ufs_log'] = [line for line in dmesg.stdout.decode(errors='replace').splitlines()
                             if re.search(r'ufs|ufshcd|unipro|m-phy|mphy', line, re.I)]
        report['dmesg_exit'] = dmesg.returncode
        if args.benchmark:
            # A failed read may already have transferred some bytes. Do not
            # leave the default 'no raw reads' assertion on a failed attempt.
            report['raw_device_reads_attempted'] = True
            report['raw_device_reads'] = None
            report['read_benchmark'] = benchmark(args.benchmark, report['blocks'])
            report['raw_device_reads'] = True
    except (OSError, ValueError, TimeoutError, subprocess.TimeoutExpired) as e:
        report['error'] = str(e)
    archive = save(report, args.output_parent.resolve(strict=True))
    print('REPORT:', archive)
    print('UFS LUNs:', ', '.join(report['blocks']) or '(none; not accepted)')
    print('NO format/partition/mount/raw device write/clock/reset/PMIC changes')
    if 'error' in report:
        p.exit(1, 'STOP: '+report['error']+'\n')
