#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import importlib.util,unittest
from pathlib import Path
s=importlib.util.spec_from_file_location('audit',Path(__file__).parent/'npu-voice/audit-openvela-npukws.py')
a=importlib.util.module_from_spec(s);s.loader.exec_module(a)
class Test(unittest.TestCase):
    def setUp(self):
        self.expected=dict(cases=[dict(tag=str(i),calls=20,hits=1,chain='12345678',names=['小瑞']) for i in range(6)])
        self.text=('npukws: resident=3 cold-prepare-us=55; file features only\n'
                   'npukws hit: word=2 name=小瑞 frame=32 probability=.2\n'
                   'npukws: status=0 calls=20 hits=1 chain=12345678 elapsed-us=999; resident released\n')*6
    def test_good(self): self.assertEqual(a.audit(self.expected,self.text)['calls'],120)
    def test_truncated(self):
        with self.assertRaises(ValueError):a.audit(self.expected,self.text[:-80])
    def test_wrong(self):
        with self.assertRaises(ValueError):a.audit(self.expected,self.text.replace('12345678','12345679',1))
    def test_failed(self):
        with self.assertRaises(ValueError):a.audit(self.expected,self.text+'npukws: status=-5\n')
    def test_no_hit(self):
        with self.assertRaises(ValueError):a.audit(self.expected,self.text.replace('name=小瑞','name=错',1))
if __name__=='__main__':unittest.main()
