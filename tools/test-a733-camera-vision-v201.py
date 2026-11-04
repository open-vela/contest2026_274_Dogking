#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""No connection/HW: strict protocol, dependency-order and real-source fixtures."""
import contextlib
import copy
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch
import zlib

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('vision201', ROOT / 'tools/a733-camera-vision-v201.py')
v = importlib.util.module_from_spec(spec)
spec.loader.exec_module(v)
TARGET = '/data/camera201test'
CRC = 0x12345678


def metadata(crc=CRC):
    return ('origin=real-published-DMA-frame\nwidth=1920\nheight=1080\nstride=3840\n'
            'bytes=4147200\npixel=raw10le-bggr\n'
            'preview=128x160-RGB24-letterbox-display-only-levels\nISP-colour-qualified=0\n'
            'model=640x640-RGB24-padding114-unlevelled\nmodel-image=640x360 pad=0,140\n'
            f'model-input-crc32={crc:08x} convention=NuttX-seed0-no-xor\n')


CAPTURE = ('frame: requested=1 attempted=1 stage=published error=0 cleanup=0 '
           'owned=0 completed=1 published=1 validation=0\n'
           f'camera: real RAW + RGB/PPM preview saved in {TARGET}; '
           f'display image {TARGET}/preview.rgb\n')


def listing(crc=CRC):
    return '\n'.join(f' -rw-rw-rw- {size} {name}' for name, size in
                     {**v.SIZES, 'COMPLETE.txt': len(metadata(crc))}.items()) + '\n'


def npu(count=3, seq=17, crc=CRC):
    result = (f'status=0 hw=0 elapsed=1686627 us irq=00000010 idle=7fffffff '
              f'polls=165 input=1228800/{crc:08x} outputs=6 candidates={count + 1} detections={count}\n')
    result += ''.join(f'output[{i}]: crc=12345678 changed=100\n' for i in range(6))
    result += ''.join(f'det[{i}]: class={i} score=0.910 box=10.00,20.00,100.00,200.00\n'
                      for i in range(count))
    return result + f'npu: vision sequence={seq} boxes={min(count, 7)} queued={min(count, 7)}\n'


def consumer(count=3, seq=17):
    result = f'display: vision frame={seq} boxes={min(count, 7)} npu=1686627 us\n'
    result += ''.join(f'display: vision det={i} class={i} score=0.910\n'
                      for i in range(min(count, 7)))
    return result + f'display: complete vision batch={seq} boxes={min(count, 7)} input-bound=1\n'


class Fake:
    def __init__(self, role, trace, fail=None, count=3):
        self.role, self.trace, self.fail, self.count = role, trace, fail, count
        self.output = ''

    def run(self, command, timeout=15):
        self.trace.append(command)
        if self.fail == command:
            raise v.Rejected('Injected command failure')
        if command == 'ls -l ' + v.MODEL:
            return f' -rw-rw-rw- {v.MODEL_SIZE} {v.MODEL}\n'
        if command == 'camera capture ' + TARGET:
            return CAPTURE
        if command == 'cat ' + TARGET + '/COMPLETE.txt':
            return metadata()
        if command == 'ls -l ' + TARGET:
            return listing()
        if command == 'npu yolo-ui ' + TARGET + '/model-rgb.bin':
            assert any(x.startswith('READY') for x in self.trace), self.trace
            return npu(self.count)
        raise AssertionError(command)

    def start(self, command):
        self.trace.append(command)
        if self.fail == command:
            raise v.Rejected('Injected start failure')
        self.output = f'display: model background input-crc32={CRC:08x} origin=file; waiting for matching inference\n'

    def wait(self, pattern, timeout=15):
        if 'model background' in pattern:
            self.trace.append('READY')
        else:
            self.trace.append('BATCH')
            assert self.trace[-2].startswith('npu yolo-ui'), self.trace
            self.output += consumer(self.count)
        if self.fail == self.trace[-1]:
            raise v.Rejected('Injected missing event')
        return self.output

    def finish(self, timeout=30):
        self.trace.append('NATURAL_EXPIRY')
        if self.fail == 'NATURAL_EXPIRY':
            raise v.Rejected('Injected UI failure after batch')
        return self.output


class ProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        (ROOT / 'out').mkdir(exist_ok=True)

    def test_real_source_contract(self):
        base = ROOT / 'board/a733-cubie-a7z/openvela-overlay/apps/system'
        display = (base / 'a733display/a733display_main.c').read_text(encoding='utf-8')
        producer = (base / 'a733npu/a733npu_main.c').read_text(encoding='utf-8')
        camera = (base / 'a733camera/a733camera_main.c').read_text(encoding='utf-8')
        for value in ('display: model background input-crc32=%08lx origin=file; waiting for matching inference',
                      'display: complete vision batch=%lu boxes=%u input-bound=%u'):
            self.assertIn(value, display)
        self.assertLess(display.index('queue = vision_queue_open();'), display.index('waiting for matching inference'))
        self.assertIn('npu: vision sequence=%lu boxes=%u queued=%u', producer)
        self.assertIn('model-input-crc32=%08lx convention=NuttX-seed0-no-xor', camera)
        config_path = ROOT / 'out/v200-release-20261028/.config'
        if config_path.is_file():
            self.assertIn('# CONFIG_DISABLE_ENVIRON is not set', config_path.read_text(encoding='utf-8'))
        else:
            # Source-only checkout: ensure the tracked board config does not
            # explicitly disable the NSH $? feature. Sealed config identity is
            # additionally checked by the local release checker when available.
            config_path = ROOT / 'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/boards/a733/cubie-a7z/configs/nsh/defconfig'
            self.assertNotIn('CONFIG_DISABLE_ENVIRON=y', config_path.read_text(encoding='utf-8'))
        self.assertIn('#define A733_NPU_RUN_MAX_DETECTIONS 32u',
                      (ROOT / 'board/a733-cubie-a7z/openvela-overlay/nuttx/include/nuttx/npu/a733_vip2.h').read_text(encoding='utf-8'))

    def test_plan_has_no_network(self):
        with contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(v.main([]), 0)
        self.assertEqual(json.loads(output.getvalue())['mode'], 'PLAN_ONLY_NO_CONNECTION')

    def test_directory_injection(self):
        for path in ('/', '/data', '/data/models', '/data/camera201;reboot',
                     '/data/camera201\nreset', '/data/camera201/../x',
                     '/data/camera201 x', '/data/camera201$(id)', '/ufs/camera201\x00'):
            with self.subTest(path=repr(path)), self.assertRaises(v.Rejected):
                v.directory(path)

    def test_metadata_mutations(self):
        for before, after in [('1920', '1280'), ('3840', '4096'), ('4147200', '0'),
                              ('bggr', 'invalid'), ('padding114', 'padding0'),
                              ('pad=0,140', 'pad=0,0'), ('NuttX-seed0-no-xor', 'zlib'),
                              ('ISP-colour-qualified=0', 'ISP-colour-qualified=1')]:
            with self.subTest(before=before), self.assertRaises(v.Rejected):
                v.manifest(metadata().replace(before, after))
        for line in metadata().splitlines(keepends=True):
            with self.subTest(line=line), self.assertRaises(v.Rejected):
                v.manifest(metadata().replace(line, ''))
        for line in metadata().splitlines(keepends=True):
            with self.subTest(duplicate=line), self.assertRaises(v.Rejected):
                v.manifest(metadata() + line)
        for order in ('bggr', 'rggb', 'gbrg', 'grbg'):
            self.assertEqual(v.manifest(metadata().replace('bggr', order))['crc32'], CRC)

    def test_capture_requires_cleanup_and_commit(self):
        v.validate_capture(CAPTURE, TARGET)
        for word in ('requested=1', 'attempted=1', 'error=0', 'cleanup=0', 'owned=0',
                     'completed=1', 'published=1', 'validation=0'):
            with self.subTest(word=word), self.assertRaises(v.Rejected):
                v.validate_capture(CAPTURE.replace(word, word[:-1] + ('0' if word[-1] == '1' else '1')), TARGET)
        with self.assertRaises(v.Rejected):
            v.validate_capture(CAPTURE.replace(TARGET, '/data/old'), TARGET)

    def test_regular_listing(self):
        paths = {TARGET + '/' + k: value for k, value in v.SIZES.items()}
        v.regular_listing(listing(), paths)
        for bad in (listing().replace('1228800', '0'), listing().replace(' -rw', ' lrw'),
                    listing() + ' -rw-rw-rw- 1 COMPLETE.partial\n',
                    listing() + listing(), listing().replace(' frame.raw', ' /data/old/frame.raw')):
            with self.subTest(bad=bad[:50]), self.assertRaises(v.Rejected):
                v.regular_listing(bad, paths)

    def test_zero_one_seven_truncated_thirtytwo(self):
        for count in (0, 1, 3, 7, 8, 16, 32):
            with self.subTest(count=count):
                p = v.producer_result(npu(count), CRC)
                v.consumer_result(consumer(count), p)
                self.assertEqual(p['boxes'], min(7, count))

    def test_producer_mutations(self):
        mutations = [('status=0', 'status=-5'), ('hw=0', 'hw=-1'), ('1228800', '0'),
                     ('input=1228800/12345678', 'input=1228800/00000000'),
                     ('outputs=6', 'outputs=5'), ('detections=3', 'detections=33'),
                     ('queued=3', 'queued=2'), ('sequence=17', 'sequence=0'),
                     ('class=1', 'class=80'), ('score=0.910', 'score=1.910'),
                     ('10.00,20.00,100.00,200.00', '10.00,20.00,641.00,200.00'),
                     ('10.00,20.00,100.00,200.00', '10.00,20.00,10.00,200.00')]
        for before, after in mutations:
            with self.subTest(before=before), self.assertRaises(v.Rejected):
                v.producer_result(npu().replace(before, after), CRC)
        for line in npu().splitlines(keepends=True):
            with self.subTest(missing=line), self.assertRaises(v.Rejected):
                v.producer_result(npu().replace(line, ''), CRC)
        for error in ('npu: vision UI not listening (2)', 'npu: vision frame dropped (11)',
                      'npu: vision detection 2 dropped (11)', 'npu: inference ioctl failed: errno=5'):
            with self.subTest(error=error), self.assertRaises(v.Rejected):
                v.producer_result(npu() + error, CRC)

    def test_consumer_mutations(self):
        p = v.producer_result(npu(), CRC)
        for before, after in [('frame=17', 'frame=16'), ('batch=17', 'batch=18'),
                              ('boxes=3', 'boxes=0'), ('input-bound=1', 'input-bound=0'),
                              ('1686627', '1686628'), ('class=1', 'class=9'),
                              ('det=1', 'det=0'), ('0.910', '0.909')]:
            with self.subTest(before=before), self.assertRaises(v.Rejected):
                v.consumer_result(consumer().replace(before, after), p)
        for line in consumer().splitlines(keepends=True):
            with self.subTest(missing=line), self.assertRaises(v.Rejected):
                v.consumer_result(consumer().replace(line, ''), p)
        with self.assertRaises(v.Rejected):
            v.consumer_result(consumer() * 2, p)
        with self.assertRaises(v.Rejected):
            v.consumer_result(consumer() + 'display: input CRC mismatch; NO boxes published', p)

    def test_dependency_order_success_and_every_failure(self):
        for count in (0, 3, 32):
            trace, report = [], {}
            v.pipeline(Fake('p', trace, count=count), Fake('v', trace, count=count), TARGET, 60, report)
            self.assertEqual(report['stage'], 'complete')
            self.assertFalse(report['accuracy_verified'])
            self.assertEqual(sum(x.startswith('camera capture') for x in trace), 1)
            self.assertEqual(sum(x.startswith('npu yolo-ui') for x in trace), 1)
        full = copy.copy(trace)
        for stop in full:
            trace, report = [], {}
            with self.subTest(stop=stop), self.assertRaises(v.Rejected):
                v.pipeline(Fake('p', trace, fail=stop), Fake('v', trace, fail=stop), TARGET, 60, report)
            self.assertEqual(trace, full[:full.index(stop) + 1])
            self.assertNotEqual(report.get('stage'), 'complete')

    def test_split_console_markers(self):
        # Exercise the REAL Shell.wait/finish without PTY/network. Echo of $? is
        # not completion. Split bytes, ANSI and prompts may surround evidence.
        for split in range(1, 51):
            shell = object.__new__(v.Shell)
            shell.output, shell.pending = '', '__A733_TEST__'
            data = 'nsh> echo __A733_TEST__:$?\r\n\x1b[K__A733_TEST__:0\r\nnsh> '
            chunks = [data[i:i + split] for i in range(0, len(data), split)]
            shell.read_chunk = lambda: chunks.pop(0)
            shell.log = io.StringIO()
            self.assertIn('__A733_TEST__:0', shell.finish())
        for data in ('__A733_TEST__:1\n',):
            shell = object.__new__(v.Shell)
            shell.output, shell.pending, shell.log = '', '__A733_TEST__', io.StringIO()
            shell.read_chunk = lambda: data
            with self.assertRaises(v.Rejected):
                shell.finish()
        shell.pending, shell.output = '__A733_TEST__', 'x' * v.LIMIT
        shell.read_chunk = lambda: 'x'
        with self.assertRaises(v.Rejected):
            shell.finish()

    def test_offline_bundle_integrity(self):
        with tempfile.TemporaryDirectory(prefix='a733-vision201-', dir=ROOT / 'out') as tmp:
            root = Path(tmp)
            rgb = bytes([114]) * (640 * 140 * 3) + bytes([60]) * (640 * 360 * 3) + bytes([114]) * (640 * 140 * 3)
            for name, payload in {'frame.raw': bytes(v.SIZES['frame.raw']),
                                  'model-rgb.bin': rgb, 'preview.rgb': bytes(61440),
                                  'preview.ppm': b'P6\n128 160\n255\n' + bytes(61440),
                                  'COMPLETE.txt': metadata(zlib.crc32(rgb, 0xffffffff) ^ 0xffffffff).encode()}.items():
                (root / name).write_bytes(payload)  # Test fixtures in private tmp only.
            before = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
            result = v.offline_bundle(root)
            self.assertFalse(result['hardware_verified'])
            self.assertEqual(before, {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()})
            bad = bytearray(rgb)
            bad[500000] ^= 1
            (root / 'model-rgb.bin').write_bytes(bad)
            with self.assertRaises(v.Rejected):
                v.offline_bundle(root)
            (root / 'model-rgb.bin').write_bytes(rgb)
            (root / 'preview.ppm').write_bytes(b'P6\n128 160\n255\n' + bytes([1]) * 61440)
            with self.assertRaises(v.Rejected):
                v.offline_bundle(root)
            (root / 'COMPLETE.partial').write_text('partial')
            with self.assertRaises(v.Rejected):
                v.offline_bundle(root)

    @unittest.skipUnless(os.name == 'posix', 'Real PTY test requires Linux/WSL; no network')
    def test_real_pty_authentication_and_command_boundaries(self):
        # Replace exec ONLY: the real PTY/auth/stream/deadline code executes.
        # This fixture is not an SSH server and never opens a socket.
        code = '''import getpass,sys
secret=getpass.getpass("openvela@test's password: ")
if secret!='NONREAL-UNIT-SECRET': sys.exit(1)
status=0
print('nsh> ',end='',flush=True)
for line in sys.stdin:
    line=line.strip()
    if line.startswith('echo '):
        print(line[5:].replace('$?',str(status)),flush=True)
    else:
        status=int(line=='fail')
        print('fixture command output',flush=True)
    print('nsh> ',end='',flush=True)
'''
        with tempfile.TemporaryDirectory(prefix='a733-pty201-', dir=ROOT / 'out') as tmp:
            path = Path(tmp) / 'fake-ssh.py'
            path.write_text(code, encoding='utf-8')
            args = SimpleNamespace(port=22, known_hosts=Path(tmp) / 'known_hosts', user='openvela', host='127.0.0.1')
            real_exec = os.execvp

            def fake_exec(name, argv):
                assert name == 'ssh' and 'StrictHostKeyChecking=yes' in argv
                assert 'UpdateHostKeys=no' in argv and argv[1:3] == ['-F', '/dev/null']
                real_exec(sys.executable, [sys.executable, str(path)])

            log = io.StringIO()
            with patch.object(v.os, 'execvp', fake_exec):
                shell = v.Shell(args, 'NONREAL-UNIT-SECRET', log)
            try:
                self.assertIn('fixture command output', shell.run('probe'))
                self.assertIsNone(shell.pending)
                with self.assertRaises(v.Rejected):
                    shell.run('fail')
                self.assertIsNone(shell.pending)
                self.assertNotIn('NONREAL-UNIT-SECRET', log.getvalue())
                self.assertIn(':1', log.getvalue())
                self.assertIn(':0', log.getvalue())
            finally:
                shell.close()

    @unittest.skipUnless(os.name == 'posix', 'Actual C exporter fixture requires Linux/WSL gcc')
    def test_actual_c_exporter_feeds_offline_validator(self):
        base = ROOT / 'board/a733-cubie-a7z/openvela-overlay/apps'
        with tempfile.TemporaryDirectory(prefix='a733-export201-', dir=ROOT / 'out') as tmp:
            root = Path(tmp)
            exe = root / 'exporter'
            subprocess.run(['gcc', '-std=c11', '-D_DEFAULT_SOURCE', '-DCONFIG_A733_CAMERA_CONTROL_TEST=1',
                            '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-fno-pie', '-no-pie',
                            '-fsanitize=address,undefined', '-I' + str(base / 'include'),
                            '-I' + str(ROOT / 'tools/camera-host-fixture'),
                            str(ROOT / 'tools/test-a733-camera-export.c'),
                            str(base / 'system/a733camera/a733_camera_image.c'), '-o', str(exe)],
                           check=True, capture_output=True, text=True, timeout=60)
            for order in ('bggr', 'rggb', 'gbrg', 'grbg'):
                bundle = root / order
                subprocess.run([str(exe), 'capture', str(bundle), order], check=True,
                               capture_output=True, text=True, timeout=30)
                result = v.offline_bundle(bundle)
                self.assertEqual(result['result'], 'OFFLINE_BUNDLE_CONSISTENT_NOT_HARDWARE')


if __name__ == '__main__':
    unittest.main(verbosity=2)
