#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Synthetic transcript parser tests, NOT hardware acceptance."""
import copy
import importlib.util
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location('logcheck', Path(__file__).parent / 'npu-voice/audit-openvela-npuref.py')
A = importlib.util.module_from_spec(spec); spec.loader.exec_module(A)


class LogTests(unittest.TestCase):
    def setUp(self):
        self.cases = [dict(model=n, case='fixed', outputs=[dict(bytes=16, raw_crc32='01234567')])
                      for n in ('joiner', 'decoder', 'encoder', 'vocoder')]
        self.cases += [dict(model='joiner', case='case%02d' % i, outputs=[dict(bytes=16, raw_crc32='01234567')])
                       for i in range(14)]
        self.log = ''
        for c in self.cases:
            self.log += 'nsh> command\nnpuref: model=%s backend=VIP2 inputs=%d outputs=1 mode=%s elapsed-us=123\n' % (
                c['model'], 39 if c['model'] == 'encoder' else 2 if c['model'] == 'joiner' else 1,
                'captured-golden' if c['case'] == 'fixed' else 'dynamic-byte-check')
            self.log += 'npuref output[0] bytes=16 raw-crc32=01234567\nnsh> \n'

    def test_full_and_fixed(self):
        self.assertEqual(A.audit(dict(cases=self.cases), self.log)['runs'], 18)
        fixed = self.log.split('npuref: model=joiner', 2)
        self.assertEqual(A.audit(dict(cases=self.cases), 'npuref: model=joiner'.join(fixed[:2]), True)['runs'], 4)

    def test_truncated_and_duplicate(self):
        for bad in (self.log[:500], self.log + self.log,
                    self.log.replace('output[0]', 'output[1]', 1)):
            with self.assertRaises(ValueError): A.audit(dict(cases=self.cases), bad)

    def test_crc_size_and_contract(self):
        for before, after in (('01234567', '01234568'), ('bytes=16', 'bytes=15'),
                              ('inputs=39', 'inputs=38'), ('captured-golden', 'dynamic-byte-check')):
            with self.assertRaises(ValueError): A.audit(dict(cases=self.cases), self.log.replace(before, after, 1))

    def test_failure_and_missing_reference(self):
        with self.assertRaises(ValueError): A.audit(dict(cases=self.cases), self.log + '\nnpuref: failed=-5')
        changed = copy.deepcopy(self.cases); changed.pop()
        with self.assertRaises(ValueError): A.audit(dict(cases=changed), self.log)


if __name__ == '__main__':
    unittest.main()
