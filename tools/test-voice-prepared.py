#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import importlib.util,json,struct,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
s=importlib.util.spec_from_file_location("exporter",Path(__file__).parent/"npu-voice/export-voice-prepared.py")
E=importlib.util.module_from_spec(s);s.loader.exec_module(E)

class ExportTests(unittest.TestCase):
    def command(self):
        words=[0x08010001,0,0x08010e02,0x30000701,0x48000000,0x30000701,
               0x08010e03,0xc23,0x08010e01,0x44,0x10000000,0,0,0,0,0]
        return struct.pack("<16I",*words)
    def test_entry_is_base_not_tail_reservation(self):
        result=E.command_audit(self.command(),8)
        self.assertEqual(result['entry_offset'],0)
        self.assertEqual(result['native_command_bytes'],8)
        self.assertEqual(result['links'],0)
    def test_unknown_or_truncated_command_rejected(self):
        for offset,word in [(0,0x40000000),(0,0x08000001),(36,0x45),(40,0),(56,1)]:
            with self.subTest(offset=offset,word=word):
                data=bytearray(self.command());struct.pack_into('<I',data,offset,word)
                with self.assertRaises(ValueError):E.command_audit(data,8)
        with self.assertRaises(ValueError):E.command_audit(self.command(),16)
    def test_region_size_and_symlink_bound(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'data';p.write_bytes(b'abc')
            self.assertEqual(E.region_read(p,3),b'abc')
            for n in (0,2,32*1024**2+1):
                with self.assertRaises(ValueError):E.region_read(p,n)
    def test_sequential_link_opt_in_only(self):
        data=bytearray(self.command())
        struct.pack_into('<2I',data,0,0x4000ffff,0x200008)
        with self.assertRaises(ValueError):E.command_audit(data,8)
        self.assertEqual(E.command_audit(data,8,0x200000)['links'],1)
        for first,second in [(0x40000001,0x200008),(0x4000ffff,0x200000),
                             (0x4000ffff,0x300000),(0x4000ffff,0x200010)]:
            struct.pack_into('<2I',data,0,first,second)
            with self.assertRaises(ValueError):E.command_audit(data,8,0x200000)
        with self.assertRaises(ValueError):E.command_audit(self.command(),8,0xfffffff0)
    def test_validate_all_before_creating_export(self):
        with tempfile.TemporaryDirectory() as d:
            report=Path(d)/'report';report.mkdir();(report/'layout-summary.json').write_text(json.dumps(dict(result='NATIVE_GOLDENS_AND_LAYOUT_CAPTURED')))
            out=Path(d)/'export'
            with patch.object(E.L,'check',return_value=({},{})),patch.object(E,'model',side_effect=ValueError('bad command')):
                with self.assertRaises(ValueError):E.export(Path(d),report,out)
            self.assertFalse(out.exists())
    def test_abi_crc_known_answer(self):
        self.assertEqual(E.H.size,128);self.assertEqual(E.TENSOR.size,64)
        self.assertEqual(E.REGION.size,32);self.assertEqual(E.crc(b'123456789'),0x2dfd2d88)
if __name__=='__main__':unittest.main()
