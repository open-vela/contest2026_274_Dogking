#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Inspect the pinned A7Z backup; optionally disable/restore its two BOOT0s.

Default is offline inspection. Physical writes require Linux, an explicit UFS
whole-LUN device, the ORIGINAL backup, an acknowledgement and an off-UFS journal.
This is NOT a generic disk eraser or a fix for unreliable SD detection.
"""
import argparse
import hashlib
import json
import os
import stat
import struct
import sys
import tempfile
import zlib
from pathlib import Path

BLOCK = 4096
PREFIX = 2097152
BLOCKS = 31256576
OFFSETS = (0x20000, 0x102000)
MAGIC = b'eGON.BT0'
ORIGINAL_SHA256 = 'a50eea041526a85c63a96420a9940e34485dba1f02fdf41185217d57615e43cd'
PARTITIONS = ((32768, 65535), (65536, 679935), (679936, 31256542))
ACK = 'DISABLE-OLD-UFS-BOOT'


def require(ok, why):
    if not ok:
        raise ValueError(why)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_prefix(path):
    require(path.is_file(), 'Backup must be an ordinary file')
    with path.open('rb') as f:
        data = f.read(PREFIX + 1)
    require(len(data) == PREFIX, 'Backup must be exactly 2097152 bytes')
    require(digest(data) == ORIGINAL_SHA256, 'Not the pinned ORIGINAL backup')
    return data


def validate_gpt(data):
    require(data[510:512] == b'\x55\xaa', 'Invalid protective MBR')
    require(any(data[450 + i * 16] == 0xee for i in range(4)), 'Missing protective MBR entry')
    h = bytearray(data[BLOCK:BLOCK + 92])
    require(h[:8] == b'EFI PART', 'Missing GPT')
    require(struct.unpack_from('<II', h, 8) == (0x10000, 92), 'Unexpected GPT header')
    crc = struct.unpack_from('<I', h, 16)[0]
    h[16:20] = bytes(4)
    require(zlib.crc32(h) == crc, 'GPT header CRC mismatch')
    require(struct.unpack_from('<QQ', h, 24) == (1, BLOCKS - 1), 'Wrong GPT capacity')
    require(struct.unpack_from('<QII', h, 72) == (2, 128, 128), 'Unexpected GPT entry area')
    table = data[2 * BLOCK:6 * BLOCK]
    require(zlib.crc32(table) == struct.unpack_from('<I', h, 88)[0], 'GPT entries CRC mismatch')
    used = []
    for i in range(128):
        entry = table[i * 128:(i + 1) * 128]
        if any(entry[:16]):
            used.append(struct.unpack_from('<QQ', entry, 32))
    require(tuple(used) == PARTITIONS, 'Unexpected partitions')
    for offset in OFFSETS:
        require(6 * BLOCK <= offset and offset + BLOCK <= PARTITIONS[0][0] * BLOCK,
                'Boot patch would overlap GPT or a partition')


def analyze(original):
    require(len(original) == PREFIX and digest(original) == ORIGINAL_SHA256, 'Backup identity mismatch')
    validate_gpt(original)
    result = {'backup_bytes': PREFIX, 'backup_sha256': digest(original),
              'logical_block_bytes': BLOCK, 'lun': 0, 'capacity_blocks': BLOCKS,
              'partitions': PARTITIONS, 'boot0': [], 'physical_device_written': False}
    for offset in OFFSETS:
        require(original[offset + 4:offset + 12] == MAGIC, 'Missing pinned BOOT0 magic')
        checksum, length = struct.unpack_from('<II', original, offset + 12)
        require(length == 245760 and offset + length <= len(original), 'Unexpected BOOT0 length')
        words = struct.unpack_from('<' + str(length // 4) + 'I', original, offset)
        computed = (sum(words) - checksum + 0x5f0a6c39) & 0xffffffff
        require(checksum == computed, 'BOOT0 checksum mismatch')
        result['boot0'].append({'offset': offset, 'offset_hex': hex(offset),
            'native_lba': offset // BLOCK, 'bytes': length, 'checksum': hex(checksum),
            'block_sha256': digest(original[offset:offset + BLOCK]),
            'image_sha256': digest(original[offset:offset + length])})
    return result


def states(current, original):
    """Only the original or exactly eight cleared magic bytes per copy allowed."""
    require(len(current) == PREFIX, 'Short device prefix')
    canonical = bytearray(current)
    enabled = []
    for offset in OFFSETS:
        marker = current[offset + 4:offset + 12]
        require(marker in (MAGIC, bytes(8)), 'Unknown/partially written boot header; STOP')
        enabled.append(marker == MAGIC)
        canonical[offset + 4:offset + 12] = MAGIC
    require(canonical == original, 'Device prefix changed outside the two permitted markers')
    return enabled


def planned(original, disabled):
    data = bytearray(original)
    if disabled:
        for offset in OFFSETS:
            data[offset + 4:offset + 12] = bytes(8)
    return bytes(data)


def read_at(fd, n, offset):
    data = os.pread(fd, n, offset)
    require(len(data) == n, 'Short device read')
    return data


def apply_blocks(fd, original, disabled, log):
    """Caller must qualify/lock the device and durably save its prefix first."""
    current = read_at(fd, PREFIX, 0)
    enabled = states(current, original)
    target = planned(original, disabled)
    # Disable backup first, then main. Restore main first, then backup.
    order = (1, 0) if disabled else (0, 1)
    for index in order:
        offset = OFFSETS[index]
        if enabled[index] == (not disabled):
            continue
        require(read_at(fd, PREFIX, 0) == current, 'Concurrent device modification; STOP')
        payload = target[offset:offset + BLOCK]
        log({'event': 'before-write', 'offset': offset, 'bytes': BLOCK})
        written = os.pwrite(fd, payload, offset)
        require(written == BLOCK, 'Short write; no automatic retry or rollback')
        os.fsync(fd)
        expected = bytearray(current)
        expected[offset:offset + BLOCK] = payload
        require(read_at(fd, PREFIX, 0) == expected, 'Readback mismatch; STOP, preserve journal')
        current = bytes(expected)
        log({'event': 'verified-write', 'offset': offset, 'prefix_sha256': digest(current)})
    require(current == target, 'Final verification failed')
    return current


def qualify_device(device):
    require(sys.platform == 'linux' and os.geteuid() == 0, 'Physical operation requires root on board Linux')
    require(not device.is_symlink() and device.parent == Path('/dev'), 'Use an explicit /dev/sdX whole-LUN node')
    info = device.stat()
    require(stat.S_ISBLK(info.st_mode), 'Target is not a block device')
    node = Path('/sys/class/block') / device.name
    require(not (node / 'partition').exists(), 'Target is a partition, not whole LUN')
    hardware = (node / 'device').resolve(strict=True)
    controller = next((p for p in hardware.parents if p.name == '4520000.ufs'), None)
    require(controller is not None and (controller / 'driver').resolve().name == 'sunxi-ufs-pltfm',
            'Target is not the pinned A733 UFS controller')
    require(hardware.name.endswith(':0'), 'Target is not LUN0')
    require((hardware / 'vendor').read_text().strip() == 'KINGSTON', 'Wrong UFS vendor')
    require((hardware / 'model').read_text().strip() == 'CY14-128', 'Wrong UFS model')
    require((hardware / 'rev').read_text().strip() == '0002', 'Wrong UFS revision')
    require(int((node / 'size').read_text()) * 512 == BLOCKS * BLOCK, 'Wrong UFS capacity')
    require(int((node / 'queue/logical_block_size').read_text()) == BLOCK, 'Wrong native block size')
    require((node / 'ro').read_text().strip() == '0', 'Device read-only; do not override it')
    relatives = [node] + [p for p in node.iterdir() if (p / 'partition').is_file()]
    devs = {p.joinpath('dev').read_text().strip() for p in relatives}
    for line in Path('/proc/self/mountinfo').read_text().splitlines():
        require(line.split()[2] not in devs, 'UFS or one of its partitions is mounted; STOP')
    for line in Path('/proc/swaps').read_text().splitlines()[1:]:
        swap = Path(line.split()[0])
        si = swap.stat()
        number = si.st_rdev if stat.S_ISBLK(si.st_mode) else si.st_dev
        require(f'{os.major(number)}:{os.minor(number)}' not in devs, 'UFS-backed swap is active')
    return info, devs


def physical(args, original):
    require(args.ack == ACK, 'Missing --ack ' + ACK)
    require(args.device is not None and args.journal_parent is not None,
            'Supply --device and --journal-parent explicitly')
    info, devs = qualify_device(args.device)
    # The backup and durable recovery journal must not live on UFS itself.
    parent = args.journal_parent.resolve(strict=True)
    for path in (args.backup, parent):
        number = path.stat().st_dev
        require(f'{os.major(number)}:{os.minor(number)}' not in devs, 'Backup/journal must be off UFS')
    mountinfo = Path('/proc/self/mountinfo').read_text().splitlines()
    matches = [line.split() for line in mountinfo if parent == Path(line.split()[4]) or
               Path(line.split()[4]) in parent.parents]
    mount = max(matches, key=lambda line: len(line[4]))
    kind = mount[mount.index('-') + 1]
    require(kind not in ('tmpfs', 'ramfs', 'devtmpfs', 'overlay', 'nfs', 'nfs4', 'cifs'),
            'Recovery journal must be on persistent local storage, not RAM/network/overlay')
    import fcntl
    # O_EXCL on a Linux block node refuses mounted/claimed targets. No force_ro.
    fd = os.open(args.device, os.O_RDWR | os.O_EXCL | os.O_NOFOLLOW)
    try:
        require(os.fstat(fd).st_rdev == info.st_rdev, 'Device node changed')
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        current = read_at(fd, PREFIX, 0)
        states(current, original)
        require(read_at(fd, PREFIX, 0) == current, 'Unstable device reads')
        journal = Path(tempfile.mkdtemp(prefix='a733-ufs-boot-', dir=parent))
        for name, data in [('original-prefix.bin', original), ('before-prefix.bin', current)]:
            with (journal / name).open('xb') as f:
                f.write(data)
                f.flush()
                os.fsync(f.fileno())
        directory = os.open(journal, os.O_RDONLY | os.O_DIRECTORY)
        os.fsync(directory)
        os.close(directory)
        directory = os.open(parent, os.O_RDONLY | os.O_DIRECTORY)
        os.fsync(directory)
        os.close(directory)
        print('Recovery journal:', journal, flush=True)
        with (journal / 'events.jsonl').open('x', encoding='utf-8') as event_file:
            directory = os.open(journal, os.O_RDONLY | os.O_DIRECTORY)
            os.fsync(directory)
            os.close(directory)
            def log(event):
                event_file.write(json.dumps(event) + '\n')
                event_file.flush()
                os.fsync(event_file.fileno())
            log({'event': 'qualified', 'device': str(args.device), 'operation': args.action,
                 'original_sha256': digest(original), 'before_sha256': digest(current)})
            result = apply_blocks(fd, original, args.action == 'disable', log)
            log({'event': 'complete', 'sha256': digest(result)})
        print('VERIFIED:', args.action, '; only the two BOOT0 magic fields changed')
    finally:
        os.close(fd)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('backup', type=Path)
    parser.add_argument('--action', choices=('inspect', 'disable', 'restore'), default='inspect')
    parser.add_argument('--device', type=Path)
    parser.add_argument('--journal-parent', type=Path)
    parser.add_argument('--ack')
    args = parser.parse_args()
    try:
        original = read_prefix(args.backup)
        print(json.dumps(analyze(original), indent=2))
        if args.action != 'inspect':
            physical(args, original)
    except (OSError, ValueError) as error:
        print('STOP:', error, file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main())
