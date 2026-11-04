#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""No board/network. Python reference vs actual production C + report binding."""
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zlib

ROOT = Path(__file__).resolve().parents[1]


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools' / filename)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


r = module('report202', 'a733-camera-report-v202.py')
t = module('fixture201', 'test-a733-camera-vision-v201.py')


def bundle(root):
    root.mkdir()
    model = bytes([114]) * (640 * 140 * 3) + bytes([128]) * (640 * 360 * 3) + bytes([114]) * (640 * 140 * 3)
    preview = bytes(44 * 128 * 3) + bytes([128]) * (72 * 128 * 3) + bytes(44 * 128 * 3)
    data = {'frame.raw': b'\0\2' * (1920 * 1080), 'model-rgb.bin': model,
            'preview.rgb': preview, 'preview.ppm': b'P6\n128 160\n255\n' + preview,
            'COMPLETE.txt': t.metadata(zlib.crc32(model, 0xffffffff) ^ 0xffffffff).encode()}
    for name, content in data.items():
        (root / name).write_bytes(content)
    return data


def logs(root, metadata, count=3):
    root.mkdir()
    target = t.TARGET
    crc = metadata['crc32']
    p = ('COMMAND camera capture ' + target + '\n' + t.CAPTURE + t.metadata(crc) +
         '\nCOMMAND npu yolo-ui ' + target + '/model-rgb.bin\n' + t.npu(count=count, crc=crc))
    d = (f'display: model background input-crc32={crc:08x} origin=file; waiting for matching inference\n'
         + t.consumer(count=count))
    summary = dict(version=201, stage='complete', result='SINGLE_FRAME_PROTOCOL_ACCEPTED_NOT_VISUAL_ACCEPTANCE',
                   metadata=metadata, inference=r.v.producer_result(p, crc),
                   **{'producer.log_sha256': r.sha(p.encode()), 'viewer.log_sha256': r.sha(d.encode())})
    (root / 'producer.log').write_text(p, encoding='utf-8', newline='\n')
    (root / 'viewer.log').write_text(d, encoding='utf-8', newline='\n')
    (root / 'summary.json').write_text(json.dumps(summary), encoding='utf-8')
    return summary


class ReportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        (ROOT / 'out').mkdir(exist_ok=True)
        cls.tmp = tempfile.TemporaryDirectory(prefix='a733-report202-', dir=ROOT / 'out')
        cls.root = Path(cls.tmp.name)
        cls.data = bundle(cls.root / 'bundle')
        cls.meta = r.v.manifest(cls.data['COMPLETE.txt'].decode())

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_00_production_source_identity(self):
        base = ROOT / 'board/a733-cubie-a7z/openvela-overlay/apps/system/a733camera'
        self.assertEqual(r.sha((base / 'a733_camera_image.c').read_bytes()), r.CPU_SOURCE_SHA)
        self.assertEqual(r.sha((base / 'a733camera_main.c').read_bytes()), r.EXPORT_SOURCE_SHA)

    def test_01_uniform_raw_reference_and_statistics(self):
        raw = r.pixels(self.data['frame.raw'])
        for order in r.PATTERNS:
            self.assertEqual(r.render_raw(raw, order, 640, 640, 114), self.data['model-rgb.bin'])
            self.assertEqual(r.render_raw(raw, order, 128, 160, 0), self.data['preview.rgb'])
        stats = r.raw_stats(raw)
        self.assertEqual((stats['min'], stats['max'], stats['mean'], stats['standard_deviation']), (512, 512, 512, 0))
        self.assertEqual(stats['histogram'][512], 1920 * 1080)
        self.assertEqual(stats['parity_means'], [512] * 4)
        self.assertEqual(stats['row_means'], [512] * 1080)
        self.assertTrue(stats['warnings'][0].startswith('UNIFORM_RAW'))
        with self.assertRaises(r.v.Rejected):
            r.pixels(b'\0\4' * (1920 * 1080))

    def test_02_mapping_padding_clipping_and_precision(self):
        self.assertEqual(r.source_box((0, 14000, 64000, 50000)), (0, 0, 192000, 108000))
        self.assertIsNone(r.source_box((100, 100, 1000, 13999)))
        self.assertIsNone(r.source_box((0, 50000, 64000, 64000)))
        self.assertEqual(r.source_box((101, 13990, 12345, 50010)), (303, 0, 37035, 108000))
        self.assertEqual(r.centi('1.01,2.02,3.03,4.04'), (101, 202, 303, 404))

    def test_03_png_chunks_and_pixels(self):
        data = r.png(2, 1, bytes([1, 2, 3, 4, 5, 6]))
        self.assertEqual(data[:8], b'\x89PNG\r\n\x1a\n')
        offset, kinds, payload = 8, [], b''
        while offset < len(data):
            n = struct.unpack('>I', data[offset:offset + 4])[0]
            kind, content = data[offset + 4:offset + 8], data[offset + 8:offset + 8 + n]
            self.assertEqual(struct.unpack('>I', data[offset + 8 + n:offset + 12 + n])[0], zlib.crc32(kind + content))
            kinds.append(kind)
            if kind == b'IDAT':
                payload += content
            offset += 12 + n
        self.assertEqual(kinds, [b'IHDR', b'IDAT', b'IEND'])
        self.assertEqual(zlib.decompress(payload), bytes([0, 1, 2, 3, 4, 5, 6]))

    def test_04_protocol_binding_zero_and_32(self):
        for count in (0, 3, 32):
            path = self.root / ('logs' + str(count))
            logs(path, self.meta, count)
            result, hashes = r.inference(path, self.meta)
            self.assertEqual(len(result['detections']), count)
            self.assertEqual(len(hashes), 3)
            images, rows = r.visualizations(self.data['model-rgb.bin'], self.data['preview.rgb'], result)
            self.assertEqual(sum(row['shown_on_lcd'] for row in rows), min(count, 7))
            self.assertEqual(len(images), 4)

    def test_05_log_rejection(self):
        path = self.root / 'log-bad'
        summary = logs(path, self.meta)
        (path / 'viewer.log').write_text('wrong', encoding='utf-8')
        with self.assertRaises(r.v.Rejected):
            r.inference(path, self.meta)
        p = self.root / 'wrong-meta'
        other = dict(self.meta, crc32=1)
        logs(p, other)
        with self.assertRaises(r.v.Rejected):
            r.inference(p, self.meta)
        with self.assertRaises(r.v.Rejected):
            r.unique_json('{"a":1,"a":2}')
        with self.assertRaises(r.v.Rejected):
            r.unique_json('{"a":NaN}')

    def test_06_full_report_no_fabricated_boxes_and_input_immutable(self):
        source, output = self.root / 'bundle', self.root / 'report'
        before = {p.name: r.sha(p.read_bytes()) for p in source.iterdir()}
        report = r.audit(source, output)
        self.assertFalse(report['inference_bound'])
        self.assertFalse(report['hardware_verified'])
        self.assertEqual(report['detections'], [])
        self.assertEqual(report['result'], 'OFFLINE_PREPROCESSING_VERIFIED_NOT_HARDWARE')
        self.assertTrue((output / 'REPORT.json').is_file())
        self.assertFalse((output / 'REPORT.partial').exists())
        for name, value in report['output_sha256'].items():
            self.assertEqual(r.sha((output / name).read_bytes()), value)
        self.assertEqual(before, {p.name: r.sha(p.read_bytes()) for p in source.iterdir()})
        with self.assertRaises(r.v.Rejected):
            r.audit(source, output)
        with self.assertRaises(r.v.Rejected):
            r.audit(source, source / 'inner')
        text = (output / 'report.html').read_text(encoding='utf-8')
        self.assertNotIn('<script', text)
        self.assertEqual(text.count('data:image/png;base64,'), 4)
        self.assertIn("default-src 'none'", text)
        escaped = r.document(report, {k: b'test' for k in report['output_sha256'] if k.endswith('.png')}, '<img src=x onerror=alert(1)>')
        self.assertIn('&lt;img src=x onerror=alert(1)&gt;', escaped)

    def test_07_crc_consistent_but_stale_model_is_rejected(self):
        path = self.root / 'stale'
        data = bundle(path)
        model = bytearray(data['model-rgb.bin'])
        model[500000] ^= 1
        (path / 'model-rgb.bin').write_bytes(model)
        (path / 'COMPLETE.txt').write_text(t.metadata(zlib.crc32(model, 0xffffffff) ^ 0xffffffff), encoding='ascii')
        r.v.offline_bundle(path)  # v201 CRC checks pass; v202 must reject RAW mismatch.
        output = self.root / 'stale-report'
        with self.assertRaises(r.v.Rejected):
            r.audit(path, output)
        self.assertFalse(output.exists())

    def test_071_crc_consistent_but_stale_preview_is_rejected(self):
        path = self.root / 'stale-preview'
        data = bundle(path)
        preview = bytearray(data['preview.rgb'])
        preview[20000] ^= 1
        (path / 'preview.rgb').write_bytes(preview)
        (path / 'preview.ppm').write_bytes(b'P6\n128 160\n255\n' + preview)
        r.v.offline_bundle(path)
        with self.assertRaises(r.v.Rejected):
            r.audit(path, self.root / 'bad-preview-report')

    def test_072_full_audit_with_bound_inference(self):
        path = self.root / 'full-bound-logs'
        logs(path, self.meta)
        result = r.audit(self.root / 'bundle', self.root / 'full-bound-report', path)
        self.assertTrue(result['inference_bound'])
        self.assertEqual(len(result['detections']), 3)
        self.assertFalse(result['hardware_verified'])

    def test_08_failed_output_does_not_publish_complete(self):
        output = self.root / 'failed-report'
        original = Path.open
        def injected(path, *args, **kwargs):
            if path.name == 'model-boxes.png' and 'x' in args[0]:
                raise OSError('injected output failure')
            return original(path, *args, **kwargs)
        with patch.object(Path, 'open', injected), self.assertRaises(OSError):
            r.audit(self.root / 'bundle', output)
        self.assertTrue(output.is_dir())
        self.assertFalse((output / 'REPORT.json').exists())

    @unittest.skipUnless(os.name == 'posix', 'Actual C/ASan oracle needs Linux/WSL gcc')
    def test_09_actual_c_export_all_bayer_orders(self):
        base = ROOT / 'board/a733-cubie-a7z/openvela-overlay/apps'
        exe = self.root / 'exporter'
        subprocess.run(['gcc', '-std=c11', '-D_DEFAULT_SOURCE', '-DCONFIG_A733_CAMERA_CONTROL_TEST=1',
                        '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-fno-pie', '-no-pie',
                        '-fsanitize=address,undefined', '-I' + str(base / 'include'),
                        '-I' + str(ROOT / 'tools/camera-host-fixture'),
                        str(ROOT / 'tools/test-a733-camera-export.c'),
                        str(base / 'system/a733camera/a733_camera_image.c'), '-o', str(exe)],
                       check=True, capture_output=True, text=True, timeout=60)
        for order in r.PATTERNS:
            source = self.root / ('actual-' + order)
            subprocess.run([str(exe), 'capture', str(source), order], check=True,
                           capture_output=True, text=True, timeout=30)
            report = r.audit(source, self.root / ('actual-report-' + order))
            self.assertTrue(report['cpu_preprocessing_exact'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
