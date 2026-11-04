#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Offline tests against a caller-supplied original prefix; NO device access."""
import importlib.util
import os
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('guard', Path(__file__).with_name('a733-ufs-boot-guard.py'))
g = importlib.util.module_from_spec(spec)
spec.loader.exec_module(g)
original = g.read_prefix(Path(sys.argv.pop(1)))


class GuardTest(unittest.TestCase):
    def test_real_backup(self):
        result = g.analyze(original)
        self.assertFalse(result['physical_device_written'])
        self.assertEqual([v['native_lba'] for v in result['boot0']], [32, 258])
        self.assertEqual([v['bytes'] for v in result['boot0']], [245760, 245760])

    def test_wrong_original(self):
        data = bytearray(original)
        data[700000] ^= 1
        with self.assertRaises(ValueError):
            g.analyze(data)

    def test_bad_gpt(self):
        for at in (510, 450, 4096, 4120, 8192):
            data = bytearray(original)
            data[at] ^= 1
            with self.subTest(at=at), self.assertRaises(ValueError):
                g.validate_gpt(data)

    def test_four_resumable_states(self):
        for mask in range(4):
            data = bytearray(original)
            for i, offset in enumerate(g.OFFSETS):
                if mask & (1 << i):
                    data[offset + 4:offset + 12] = bytes(8)
            self.assertEqual(g.states(data, original), [not mask & 1, not mask & 2])

    def test_any_other_mutation_rejected(self):
        for at in (0, 4096, 8192, 0x20000, 0x2000c, 0x20010, 0x102020, 2000000):
            data = bytearray(original)
            data[at] ^= 1
            with self.subTest(at=at), self.assertRaises(ValueError):
                g.states(data, original)

    def test_partial_marker_rejected(self):
        for offset in g.OFFSETS:
            data = bytearray(original)
            data[offset + 4] = 0
            with self.assertRaises(ValueError):
                g.states(data, original)

    def run_fake(self, before, disabled, *, short=False, corrupt=False, concurrent=False, fail_log=False):
        media = bytearray(before)
        writes, logs = [], []
        reads = 0

        def pread(fd, n, offset):
            nonlocal reads
            self.assertEqual(fd, 123)
            reads += 1
            if concurrent and reads == 2:
                media[700000] ^= 1
            return bytes(media[offset:offset + n])

        def pwrite(fd, payload, offset):
            self.assertEqual(fd, 123)
            self.assertIn(offset, g.OFFSETS)
            self.assertEqual(len(payload), 4096)
            writes.append(offset)
            media[offset:offset + len(payload)] = payload
            if corrupt:
                media[offset + 100] ^= 1
            return len(payload) - 1 if short else len(payload)

        def log(event):
            if fail_log:
                raise OSError('journal fsync failed')
            logs.append(event)

        with patch.object(os, 'pread', pread, create=True), \
             patch.object(os, 'pwrite', pwrite, create=True), \
             patch.object(os, 'fsync', lambda fd: self.assertEqual(fd, 123)):
            if short or corrupt or concurrent or fail_log:
                with self.assertRaises((ValueError, OSError)):
                    g.apply_blocks(123, original, disabled, log)
                self.assertLessEqual(len(writes), 1)  # NO retry/automatic rollback
                if concurrent or fail_log:
                    self.assertEqual(writes, [])
            else:
                result = g.apply_blocks(123, original, disabled, log)
                self.assertEqual(result, g.planned(original, disabled))
                self.assertEqual(bytes(media), result)
        return writes

    def test_disable_only_two_blocks(self):
        self.assertEqual(self.run_fake(original, True), [0x102000, 0x20000])
        target = g.planned(original, True)
        changes = [i for i, (a, b) in enumerate(zip(original, target)) if a != b]
        self.assertEqual(changes, [offset + k for offset in g.OFFSETS for k in range(4, 12)])

    def test_restore_byte_exact(self):
        self.assertEqual(self.run_fake(g.planned(original, True), False), [0x20000, 0x102000])

    def test_idempotent(self):
        self.assertEqual(self.run_fake(g.planned(original, True), True), [])
        self.assertEqual(self.run_fake(original, False), [])

    def test_resume_one_disabled(self):
        for i, offset in enumerate(g.OFFSETS):
            data = bytearray(original)
            data[offset + 4:offset + 12] = bytes(8)
            self.assertEqual(self.run_fake(data, True), [g.OFFSETS[1-i]])

    def test_short_write_no_retry(self):
        self.run_fake(original, True, short=True)

    def test_bad_readback_no_retry(self):
        self.run_fake(original, True, corrupt=True)

    def test_concurrent_mutation_no_write(self):
        self.run_fake(original, True, concurrent=True)

    def test_failed_journal_no_write(self):
        self.run_fake(original, True, fail_log=True)

    def test_short_read(self):
        with patch.object(os, 'pread', lambda *args: bytes(10), create=True), self.assertRaises(ValueError):
            g.read_at(123, 4096, 0)


if __name__ == '__main__':
    unittest.main()
