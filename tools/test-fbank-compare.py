#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import importlib.util
import math
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

script = Path(__file__).parent / 'npu-voice/compare-fbank.py'
spec = importlib.util.spec_from_file_location('compare_fbank', script)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class TestCompare(unittest.TestCase):
    def test_contract(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            a, b = root/'a.f32', root/'b.f32'
            data = bytearray(29*320)
            a.write_bytes(data)
            b.write_bytes(data)
            self.assertTrue(module.compare(a, b)['byte_exact'])
            struct.pack_into('<f', data, 324, 0.125)
            b.write_bytes(data)
            result = module.compare(a, b)
            self.assertFalse(result['byte_exact'])
            self.assertEqual(result['differing'], 1)
            self.assertEqual(result['first']['frame'], 1)
            self.assertEqual(result['first']['bin'], 1)
            self.assertEqual(result['maximum_absolute'], 0.125)
            self.assertAlmostEqual(result['rmse'], 0.125/math.sqrt(29*80))
            run = subprocess.run([sys.executable, str(script), str(a), str(b)], capture_output=True)
            self.assertEqual(run.returncode, 2)
            report = root/'report.json'
            report.write_text('preserve')
            run = subprocess.run([sys.executable, str(script), str(a), str(a), '--json', str(report)], capture_output=True)
            self.assertEqual(run.returncode, 1)
            self.assertEqual(report.read_text(), 'preserve')
            # Signed zero differs in bytes even though absolute error is zero.
            data = bytearray(29*320)
            struct.pack_into('<f', data, 0, -0.0)
            b.write_bytes(data)
            result = module.compare(a, b)
            self.assertFalse(result['byte_exact'])
            self.assertEqual(result['maximum_absolute'], 0)
            for value in [float('nan'), float('inf'), 1001.0]:
                struct.pack_into('<f', data, 0, value)
                b.write_bytes(data)
                with self.assertRaises(ValueError): module.compare(a, b)
            for size in [0, 29*320-1, 1001*320, 30*320]:
                b.write_bytes(bytes(size))
                with self.assertRaises(ValueError): module.compare(a, b)


if __name__ == '__main__': unittest.main()
