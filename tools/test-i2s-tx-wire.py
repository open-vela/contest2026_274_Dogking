#!/usr/bin/env python3
"""Synthetic checks only, not a replacement for physical capture."""
import importlib.util
from pathlib import Path
import unittest
import numpy as np

spec = importlib.util.spec_from_file_location('audit', Path(__file__).with_name('audit-i2s-tx-wire.py'))
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


def fixture(zero=False):
    table = [0,3196,6269,9102,11585,13622,15136,16069,16384,
             16069,15136,13622,11585,9102,6269,3196,0,-3196,-6269,-9102,
             -11585,-13622,-15136,-16069,-16384,-16069,-15136,-13622,
             -11585,-9102,-6269,-3196]
    a = np.zeros(300*64*8, dtype=np.uint8)
    for frame in range(300):
        sample = 0 if zero else table[((((48000+frame)*((1000 << 32)//48000)) & 0xffffffff) >> 27)]
        for ch in range(2):
            for bit in range(32):
                data = ((sample >> (16-bit)) & 1) if 1 <= bit <= 16 else 0
                at = (frame*64+ch*32+bit)*8
                a[at:at+8] = (ch << 1) | (data << 2)
                a[at+4:at+8] |= 1
    return a


class Tests(unittest.TestCase):
    def test_standard_s16(self):
        r = audit.decode_window(fixture(), 24576000, 0, 1, 2)
        self.assertEqual(r['bclk_hz'], 3072000)
        self.assertEqual(r['lrclk_hz'], 48000)
        self.assertTrue(r['exact_driver_1khz_s16_pattern'])
        self.assertTrue(r['left_equals_right'])
        self.assertEqual(r['left']['maximum'], 16384)

    def test_silent_data_not_tone(self):
        r = audit.decode_window(fixture(True), 24576000, 0, 1, 2)
        self.assertEqual(r['left']['nonzero'], 0)
        self.assertFalse(r['exact_driver_1khz_s16_pattern'])

    def test_wrong_map_rejected(self):
        with self.assertRaises(ValueError):
            audit.decode_window(fixture(), 24576000, 1, 0, 2)

    def test_undersampling_rejected(self):
        with self.assertRaises(ValueError):
            audit.decode_window(fixture(), 20000, 0, 1, 2)

    def test_duplicate_probe_rejected(self):
        with self.assertRaises(ValueError):
            audit.decode_window(fixture(), 24576000, 0, 0, 2)


if __name__ == '__main__':
    unittest.main()
