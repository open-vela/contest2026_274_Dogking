#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""v201 host-only single-frame coordinator; v200 firmware is unchanged.

Default: print plan, no connection. --verify-bundle is read-only/offline.
--run uses Linux/WSL OpenSSH + PTYs, two NSH sessions, strict known_hosts.
No reset/retry/format/PMIC writes, no forced remote cancellation.
"""
import argparse
import getpass
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import re
import sys
import time
import uuid
import zlib

MODEL = '/data/npu/yolov8n-pcq-v3.a7pm'
MODEL_SIZE = 8017024
MODEL_SHA = '3c2891ef77ce9a44dd02cd32bdd7db5ec92d14185014e6de8db5803a6cbecce2'
FIRMWARE_SHA = '19e2e0772816a44ee9ec671c3eac92f7978b2587a8a8c502c7abd60919cf96c0'
IMAGE_SHA = 'a729b7442ffc764063b191d8ac3ebb38c4a2812c18f21cc9dacf2cd02fbaaa33'
SIZES = {'frame.raw': 4147200, 'preview.rgb': 61440,
         'preview.ppm': 61455, 'model-rgb.bin': 1228800}
ANSI = re.compile(r'\x1b\[[0-?]*[ -/]*[@-~]')
FAILURE = re.compile(r'CRC mismatch|dropped \(|not listening|invalid vision|'
                     r'ignored invalid|SPI.*fail|flush.*fail|inference ioctl failed', re.I)
LIMIT = 256 * 1024


class Rejected(ValueError):
    pass


def require(condition, reason):
    if not condition:
        raise Rejected(reason)


def clean(text):
    return ANSI.sub('', text.replace('\r', '')).replace('nsh> ', '')


def exactly(pattern, text, name):
    matches = re.findall(pattern, clean(text), re.M)
    require(len(matches) == 1, name + ': expected exactly one record')
    return matches[0]


def directory(value):
    require(bool(re.fullmatch(r'/(?:data|ufs)/camera201[a-zA-Z0-9_-]{0,40}', value)),
            'Use a NEW /data/camera201NAME or /ufs/camera201NAME directory')
    return value


def manifest(text):
    fields = {}
    for line in clean(text).splitlines():
        if '=' not in line:
            continue
        key, value = line.split('=', 1)
        if key in ('origin', 'width', 'height', 'stride', 'bytes', 'pixel',
                   'preview', 'ISP-colour-qualified', 'model', 'model-image',
                   'model-input-crc32'):
            require(key not in fields, 'Duplicate COMPLETE key: ' + key)
            fields[key] = value
    expected = {'origin': 'real-published-DMA-frame', 'width': '1920',
                'height': '1080', 'stride': '3840', 'bytes': '4147200',
                'preview': '128x160-RGB24-letterbox-display-only-levels',
                'ISP-colour-qualified': '0',
                'model': '640x640-RGB24-padding114-unlevelled',
                'model-image': '640x360 pad=0,140'}
    require(all(fields.get(k) == v for k, v in expected.items()),
            'Missing/incompatible COMPLETE geometry or preprocessing')
    require(fields.get('pixel') in ('raw10le-bggr', 'raw10le-rggb',
                                   'raw10le-gbrg', 'raw10le-grbg'), 'Unsupported Bayer order')
    value = fields.get('model-input-crc32', '')
    require(bool(re.fullmatch(r'[0-9a-fA-F]{8} convention=NuttX-seed0-no-xor', value)),
            'Missing/incompatible NuttX CRC convention')
    return {'fields': fields, 'crc32': int(value[:8], 16)}


def regular_listing(text, paths):
    found = {}
    for mode, size, path in re.findall(r'^\s*(-[rwx-]{9})\s+(\d+)\s+(\S+)\s*$',
                                     clean(text), re.M):
        name = path.rsplit('/', 1)[-1]
        require(name not in found, 'Duplicate ls entry: ' + name)
        found[name] = (int(size), path)
    require('COMPLETE.partial' not in found, 'Uncommitted/ambiguous capture')
    for path, size in paths.items():
        name = path.rsplit('/', 1)[-1]
        require(name in found and found[name][0] == size and
                found[name][1] in (name, path), 'Missing/wrong regular file: ' + path)


def validate_capture(text, target):
    fields = exactly(r'^frame: requested=1 attempted=1 stage=(\S+) error=0 cleanup=0 '
                     r'owned=0 completed=1 published=1 validation=0$', text, 'Published RAW')
    require('camera: real RAW + RGB/PPM preview saved in ' + target + ';' in clean(text),
            'Capture did not commit the requested NEW directory')
    require('NO new COMPLETE' not in text, 'Capture failure')
    return fields


def producer_result(text, crc):
    require(not FAILURE.search(text), 'NPU publication/transport failed')
    values = exactly(r'^status=(-?\d+) hw=(-?\d+) elapsed=(\d+) us irq=([0-9a-fA-F]{8}) '
                     r'idle=([0-9a-fA-F]{8}) polls=(\d+) input=(\d+)/([0-9a-fA-F]{8}) '
                     r'outputs=(\d+) candidates=(\d+) detections=(\d+)$', text, 'NPU run')
    status, hw, elapsed, irq, idle, polls, size, actual, outputs, candidates, count = values
    require(status == hw == '0' and int(size) == 1228800 and int(actual, 16) == crc
            and int(outputs) == 6 and int(count) <= 32 and int(candidates) >= int(count),
            'NPU status/input/output identity mismatch')
    output_records = re.findall(r'^output\[(\d+)\]: crc=([0-9a-fA-F]{8}) changed=(\d+)$',
                                clean(text), re.M)
    require([int(row[0]) for row in output_records] == list(range(6)), 'Missing raw output CRCs')
    seq, boxes, queued = exactly(r'^npu: vision sequence=(\d+) boxes=(\d+) queued=(\d+)$',
                                text, 'MQ producer')
    require(0 < int(seq) <= 0xffffffff and int(boxes) == min(int(count), 7)
            and int(queued) == int(boxes), 'Incomplete detection publication')
    detections = re.findall(r'^det\[(\d+)\]: class=(\d+) score=(\d+)\.(\d{3}) '
                           r'box=([^\n]+)$', clean(text), re.M)
    require([int(d[0]) for d in detections] == list(range(int(count))), 'Missing detections')
    for index, cls, whole, part, box in detections:
        require(int(cls) < 80 and int(whole) * 1000 + int(part) <= 1000,
                'Malformed detection class/score')
        require(bool(re.fullmatch(r'\d+\.\d{2},\d+\.\d{2},\d+\.\d{2},\d+\.\d{2}', box)),
                'Malformed detection coordinates')
        x1, y1, x2, y2 = map(float, box.split(','))
        require(0 <= x1 < x2 <= 640 and 0 <= y1 < y2 <= 640, 'Out-of-range/empty detection')
    return dict(sequence=int(seq), boxes=int(boxes), detections=detections,
                npu_elapsed_us=int(elapsed), input_crc32=f'{crc:08x}', outputs=output_records)


def consumer_result(text, result):
    require(not FAILURE.search(text), 'Display rejected a message/SPI operation')
    seq, count, elapsed = exactly(r'^display: vision frame=(\d+) boxes=(\d+) npu=(\d+) us$',
                                 text, 'MQ frame')
    complete = exactly(r'^display: complete vision batch=(\d+) boxes=(\d+) input-bound=(\d+)$',
                       text, 'MQ complete batch')
    require(tuple(map(int, (seq, count, elapsed))) ==
            (result['sequence'], result['boxes'], result['npu_elapsed_us']) and
            tuple(map(int, complete)) == (result['sequence'], result['boxes'], 1),
            'Stale/unbound/wrong sequence, count or latency')
    shown = re.findall(r'^display: vision det=(\d+) class=(\d+) score=(\d+)\.(\d{3})$',
                       clean(text), re.M)
    require(shown == [row[:4] for row in result['detections'][:result['boxes']]],
            'Displayed detection indices/classes/scores differ')


def offline_bundle(path):
    require(path.is_dir() and not path.is_symlink(), 'Bundle must be a real directory')
    require(not (path / 'COMPLETE.partial').exists(), 'Incomplete bundle')
    files = {}
    for name, size in {**SIZES, 'COMPLETE.txt': None}.items():
        file = path / name
        require(not file.is_symlink() and file.is_file(), 'Missing/nonregular: ' + name)
        require(file.stat().st_size == size if size else 0 < file.stat().st_size <= 512,
                'Invalid file size: ' + name)
        data = file.read_bytes()
        files[name] = dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
    metadata = manifest((path / 'COMPLETE.txt').read_text(encoding='ascii'))
    rgb = (path / 'model-rgb.bin').read_bytes()
    crc = zlib.crc32(rgb, 0xffffffff) ^ 0xffffffff
    require(crc == metadata['crc32'], 'Bundle model CRC mismatch')
    require(rgb[:640 * 140 * 3] == bytes([114]) * (640 * 140 * 3) and
            rgb[640 * 500 * 3:] == bytes([114]) * (640 * 140 * 3), 'Wrong model padding')
    require((path / 'preview.ppm').read_bytes() == b'P6\n128 160\n255\n' +
            (path / 'preview.rgb').read_bytes(), 'PPM and RGB preview differ')
    return dict(result='OFFLINE_BUNDLE_CONSISTENT_NOT_HARDWARE', files=files,
                input_crc32=f'{crc:08x}', accuracy_verified=False, hardware_verified=False)


def pipeline(producer, viewer, target, seconds, report):
    """Interfaces are also used by the deterministic fake-transport tests."""
    directory(target)
    start = time.monotonic()
    regular_listing(producer.run('ls -l ' + MODEL), {MODEL: MODEL_SIZE})
    report['stage'] = 'capture-one-shot'
    validate_capture(producer.run('camera capture ' + target, timeout=45), target)
    text = producer.run('cat ' + target + '/COMPLETE.txt')
    metadata = manifest(text)
    paths = {target + '/' + k: v for k, v in SIZES.items()}
    paths[target + '/COMPLETE.txt'] = len(('\n'.join(
        k + '=' + v for k, v in metadata['fields'].items()) + '\n').encode('ascii'))
    regular_listing(producer.run('ls -l ' + target), paths)
    report.update(stage='display-readiness', metadata=metadata,
                  capture_and_export_wall_ms=round((time.monotonic() - start) * 1000))
    viewer.start('display vision-image ' + target + '/model-rgb.bin ' + str(seconds))
    ready = viewer.wait(r'display: model background input-crc32=([0-9a-fA-F]{8}) '
                        r'origin=file; waiting for matching inference', timeout=15)
    require(not FAILURE.search(ready), 'Display readiness failed')
    ready_crc = exactly(r'display: model background input-crc32=([0-9a-fA-F]{8}) '
                        r'origin=file; waiting for matching inference', ready, 'UI readiness')
    require(int(ready_crc, 16) == metadata['crc32'], 'UI background differs from COMPLETE')
    report['stage'] = 'npu-single-run'
    begin = time.monotonic()
    result = producer_result(producer.run('npu yolo-ui ' + target + '/model-rgb.bin', timeout=30),
                             metadata['crc32'])
    report['stage'] = 'display-complete-batch'
    viewer.wait(r'display: complete vision batch=\d+ boxes=\d+ input-bound=\d+', timeout=10)
    consumer_result(viewer.output, result)
    report.update(inference=result, infer_to_batch_wall_ms=round((time.monotonic() - begin) * 1000))
    # Let ONLY the UI expire naturally; never send Ctrl+C to a DMA owner.
    viewer.finish(timeout=seconds + 5)
    consumer_result(viewer.output, result)
    report.update(stage='complete', result='SINGLE_FRAME_PROTOCOL_ACCEPTED_NOT_VISUAL_ACCEPTANCE',
                  total_wall_ms=round((time.monotonic() - start) * 1000),
                  fresh_capture_command=True, accuracy_verified=False,
                  physical_image_verified=False, fps_measured=False,
                  firmware_identity_independently_verified=False,
                  model_sha256_independently_verified=False)


class Shell:
    """Linux PTY OpenSSH. Authentication output/password is NEVER recorded."""
    def __init__(self, args, secret, log):
        import pty
        import select
        import signal
        self.select, self.signal = select, signal
        self.log, self.output, self.cursor, self.pending = log, '', 0, None
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            try:
                os.execvp('ssh', ['ssh', '-F', '/dev/null', '-tt', '-p', str(args.port),
                          '-o', 'ConnectTimeout=8', '-o', 'StrictHostKeyChecking=yes',
                          '-o', 'UpdateHostKeys=no', '-o', 'GlobalKnownHostsFile=/dev/null',
                          '-o', 'UserKnownHostsFile=' + str(args.known_hosts.resolve()),
                          '-o', 'PreferredAuthentications=password', '-o', 'PubkeyAuthentication=no',
                          '-o', 'NumberOfPasswordPrompts=1', args.user + '@' + args.host])
            except OSError:
                os.write(2, b'Local OpenSSH executable could not start\n')
                os._exit(127)
        self.alive = True
        try:
            auth, sent, deadline = '', False, time.monotonic() + 20
            while time.monotonic() < deadline:
                auth = (auth + self.read_chunk())[-4096:]
                if not sent and re.search(r'password:\s*$', auth, re.I):
                    os.write(self.fd, secret.encode('utf-8') + b'\n')
                    sent = True
                    auth = ''
                if sent and re.search(r'nsh>\s*$', ANSI.sub('', auth.replace('\r', ''))):
                    return
            raise Rejected('SSH authentication/NSH deadline; check known_hosts/network')
        except BaseException:
            self.close()
            raise

    def read_chunk(self):
        if not self.select.select([self.fd], [], [], 0.1)[0]:
            return ''
        try:
            data = os.read(self.fd, 8192)
        except OSError as error:
            raise Rejected('SSH transport closed; remote state uncertain') from error
        require(bool(data), 'SSH EOF; remote state uncertain')
        return data.decode('utf-8', errors='replace')

    def start(self, command):
        require(self.pending is None, 'Session already has a command')
        self.output, self.cursor = '', 0
        self.pending = '__A733_' + uuid.uuid4().hex + '__'
        self.log.write('\nCOMMAND ' + command + '\n')
        self.log.flush()
        os.write(self.fd, (command + '\necho ' + self.pending + ':$?\n').encode('ascii'))

    def wait(self, pattern, timeout=15):
        deadline = time.monotonic() + timeout
        last_notice = time.monotonic()
        while True:
            if re.search(pattern, clean(self.output)):
                return self.output
            if self.pending and re.search(re.escape(self.pending) + r':[01](?:\n|$)', clean(self.output)):
                raise Rejected('Command ended before required evidence')
            require(time.monotonic() < deadline, 'Command evidence deadline; no automatic retry/reset')
            chunk = self.read_chunk()
            self.output += chunk
            require(len(self.output.encode('utf-8')) <= LIMIT, 'Console transcript bound exceeded')
            self.log.write(chunk)
            self.log.flush()
            if time.monotonic() - last_notice >= 15:
                print('Waiting for bounded board operation / natural UI expiry...', flush=True)
                last_notice = time.monotonic()

    def finish(self, timeout=30):
        require(self.pending is not None, 'No pending command')
        pattern = re.escape(self.pending) + r':([01])(?:\n|$)'
        # wait() checks desired evidence before checking terminal marker.
        text = self.wait(pattern, timeout)
        result = exactly(pattern, text, 'NSH exit marker')
        self.pending = None
        require(result == '0', 'NSH command returned failure (1)')
        return text

    def run(self, command, timeout=15):
        print(command, flush=True)
        self.start(command)
        return self.finish(timeout)

    def settle(self, timeout):
        if self.pending is not None:
            try:
                self.finish(timeout)
            except (Rejected, OSError):
                pass

    def close(self):
        if not getattr(self, 'alive', False):
            return
        self.alive = False
        os.close(self.fd)
        pid, status = os.waitpid(self.pid, os.WNOHANG)
        if not pid:
            # Local SSH process only. Never a remote kill/reset/retry command.
            try:
                os.kill(self.pid, self.signal.SIGTERM)
            except ProcessLookupError:
                pass
            os.waitpid(self.pid, 0)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument('--run', action='store_true')
    modes.add_argument('--verify-bundle', type=Path)
    parser.add_argument('--host', help='Current board IPv4/IPv6 literal; no hardcoded old IP')
    parser.add_argument('--user', default='openvela')
    parser.add_argument('--port', type=int, default=22)
    parser.add_argument('--known-hosts', type=Path)
    parser.add_argument('--new-directory', default='/data/camera201a')
    parser.add_argument('--display-seconds', type=int, default=60)
    parser.add_argument('--result', type=Path, help='NEW local evidence directory')
    args = parser.parse_args(argv)
    directory(args.new_directory)
    require(45 <= args.display_seconds <= 120, 'Display duration must be 45..120 seconds')
    require(1 <= args.port <= 65535 and bool(re.fullmatch(r'[a-zA-Z0-9_]{1,32}', args.user)),
            'Invalid SSH user/port')
    if args.verify_bundle:
        print(json.dumps(offline_bundle(args.verify_bundle), indent=2))
        return 0
    if not args.run:
        print(json.dumps(dict(mode='PLAN_ONLY_NO_CONNECTION',
              stages=['fresh one-shot capture', 'COMPLETE/files', 'UI readiness + input CRC',
                      'one YOLO run', 'same sequence complete batch', 'natural UI expiry'],
              target=args.new_directory, firmware='v200 unchanged', expected_image_sha256=IMAGE_SHA,
              no_hardware_acceptance=True), indent=2))
        return 0
    require(os.name == 'posix', '--run requires Linux/WSL + OpenSSH; no pip dependencies')
    require(args.host is not None, '--host is required')
    ipaddress.ip_address(args.host)
    require(args.known_hosts is not None and args.known_hosts.is_file(),
            '--known-hosts must name an existing trusted file; no automatic key acceptance')
    require(args.result is not None and not args.result.exists(), '--result must be a NEW directory')
    args.result.mkdir(parents=True, exist_ok=False)
    report = dict(version=201, stage='connecting', result='NOT_COMPLETED',
                  host=args.host, expected_firmware_sha256=FIRMWARE_SHA,
                  expected_image_sha256=IMAGE_SHA, expected_model_sha256=MODEL_SHA,
                  hardware_verified=False, reset_retry_format_or_pmic_writes=False)
    producer = viewer = None
    try:
        secret = getpass.getpass('Board SSH password (not logged): ')
        require(bool(secret) and len(secret.encode('utf-8')) <= 128 and
                not any(c in secret for c in '\r\n\x00'), 'Invalid SSH password length/control characters')
        with (args.result / 'producer.log').open('x', encoding='utf-8') as p_log, \
             (args.result / 'viewer.log').open('x', encoding='utf-8') as v_log:
            try:
                producer = Shell(args, secret, p_log)
                viewer = Shell(args, secret, v_log)
                secret = None
                pipeline(producer, viewer, args.new_directory, args.display_seconds, report)
            finally:
                secret = None
                # Do not Ctrl+C capture/NPU even when a stage failed. One bounded
                # grace wait for pending operations; transport loss stays UNKNOWN.
                if producer:
                    producer.settle(35)
                if viewer:
                    viewer.settle(args.display_seconds + 5)
    except (Rejected, OSError, KeyboardInterrupt) as error:
        report.update(result='REJECTED_OR_INTERRUPTED', error=str(error),
                      recovery='Inspect serial camera control status / npu report. No automatic retry.')
    finally:
        for shell in (viewer, producer):
            if shell:
                shell.close()
        for name in ('producer.log', 'viewer.log'):
            path = args.result / name
            if path.is_file():
                report[name + '_sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
        with (args.result / 'summary.json').open('x', encoding='utf-8') as stream:
            json.dump(report, stream, indent=2)
    print(json.dumps(report, indent=2))
    return 0 if report['result'] == 'SINGLE_FRAME_PROTOCOL_ACCEPTED_NOT_VISUAL_ACCEPTANCE' else 2


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (Rejected, OSError, ValueError) as error:
        print('STOP:', error, file=sys.stderr)
        sys.exit(2)
