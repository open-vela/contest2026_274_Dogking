#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""No device/network access: sysfs fixtures, safety refusal and archive hashes."""
import hashlib
import importlib.util
import json
import tarfile
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch
from pathlib import Path

spec = importlib.util.spec_from_file_location('collector', Path(__file__).with_name('collect-a733-ufs-linux.py'))
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)


class TestCollector(unittest.TestCase):
    def electrical_fixture(self, root):
        dt = root/'sys/firmware/devicetree/base'
        node = dt/'soc/ufs@4520000'
        node.mkdir(parents=True)
        ctrl = root/'sys/bus/platform/devices/4520000.ufs'
        ctrl.mkdir(parents=True)
        (ctrl/'of_node').symlink_to(node, target_is_directory=True)
        (ctrl/'driver').symlink_to(root/'sys/bus/platform/drivers/sunxi-ufs-pltfm')
        for index, (supply, name, uv) in enumerate([
                ('vcc', 'dldo6', 2500000), ('vccq', 'dcdc8', 1200000)]):
            target = dt/('pmic/'+name)
            target.mkdir(parents=True)
            (target/'phandle').write_bytes((index+1).to_bytes(4, 'big'))
            # A second spelling of the SAME node is not an ambiguous phandle.
            (target/'linux,phandle').write_bytes((index+1).to_bytes(4, 'big'))
            (target/'regulator-name').write_bytes(name.encode()+b'\0')
            for key, value in [('regulator-min-microvolt', 500000),
                               ('regulator-max-microvolt', 3400000)]:
                (target/key).write_bytes(value.to_bytes(4, 'big'))
            (node/(supply+'-supply')).write_bytes((index+1).to_bytes(4, 'big'))
            reg = root/('sys/class/regulator/regulator.'+str(index))
            reg.mkdir(parents=True)
            (reg/'of_node').symlink_to(target, target_is_directory=True)
            for key, value in [('name', name), ('microvolts', str(uv)),
                               ('state', 'enabled')]:
                (reg/key).write_text(value)
        (node/'vccq2-supply').write_bytes((2).to_bytes(4, 'big'))
        return dt, node

    def test_electrical_linked_readback_not_acceptance(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.electrical_fixture(root)
            result = c.electrical_evidence(root)
            self.assertTrue(result['controller_bound'])
            self.assertFalse(result['cold_start_authorized'])
            self.assertFalse(result['ordering_suffix_confirmed'])
            self.assertFalse(result['physical_voltage_verified'])
            self.assertFalse(result['dma_mapping_verified'])
            for supply in ('vcc', 'vccq'):
                self.assertTrue(result['supplies'][supply]['configured_range_match'])
            legacy = result['supplies']['vccq2']
            self.assertEqual(legacy['node'], result['supplies']['vccq']['node'])
            self.assertIsNone(legacy['configured_range_match'])
            self.assertIn('NC-ball', legacy['reason'])

    def test_electrical_unknown_and_out_of_range(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            dt, node = self.electrical_fixture(root)
            reg = root/'sys/class/regulator/regulator.0'
            for uv, expected in [('2400000', True), ('2700000', True),
                                 ('3300000', False), ('0', False),
                                 ('-22', None), ('not readable', None)]:
                (reg/'microvolts').write_text(uv)
                value = c.electrical_evidence(root)['supplies']['vcc']
                self.assertIs(value['configured_range_match'], expected)
            (reg/'microvolts').unlink()
            self.assertIsNone(c.electrical_evidence(root)['supplies']['vcc']['configured_range_match'])
            (node/'vcc-supply').write_bytes(b'\1\2')
            self.assertEqual(c.electrical_evidence(root)['supplies']['vcc']['resolution'], 'unknown')
            (node/'vcc-supply').write_bytes((0).to_bytes(4, 'big'))
            self.assertEqual(c.electrical_evidence(root)['supplies']['vcc']['resolution'], 'unknown')
            qreg = root/'sys/class/regulator/regulator.1'
            for uv, expected in [('1140000', True), ('1260000', True),
                                 ('1800000', False), ('1139999', False),
                                 ('1260001', False)]:
                (qreg/'microvolts').write_text(uv)
                self.assertIs(c.electrical_evidence(root)['supplies']['vccq']['configured_range_match'], expected)

    def test_electrical_no_name_guessing_or_ambiguous_node(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            dt, node = self.electrical_fixture(root)
            # Same name with an absent/unrelated of_node must not be evidence.
            reg = root/'sys/class/regulator/regulator.0'
            (reg/'of_node').unlink()
            result = c.electrical_evidence(root)['supplies']['vcc']
            self.assertFalse(result['sysfs'])
            self.assertIsNone(result['configured_range_match'])
            duplicate = dt/'other'
            duplicate.mkdir()
            (duplicate/'phandle').write_bytes((1).to_bytes(4, 'big'))
            self.assertEqual(c.electrical_evidence(root)['supplies']['vcc']['resolution'], 'unknown')
            (root/'sys/bus/platform/devices/4520000.ufs/of_node').unlink()
            result = c.electrical_evidence(root)
            self.assertFalse(result['supplies'])
            self.assertIn('No controller-linked', result['error'])

    def test_electrical_conflicting_alias_and_duplicate_regulator(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            dt, node = self.electrical_fixture(root)
            reg = root/'sys/class/regulator/regulator.2'
            reg.mkdir()
            (reg/'of_node').symlink_to(dt/'pmic/dldo6', target_is_directory=True)
            self.assertIsNone(c.electrical_evidence(root)['supplies']['vcc']['configured_range_match'])
            (dt/'pmic/dldo6/linux,phandle').write_bytes((7).to_bytes(4, 'big'))
            result = c.electrical_evidence(root)
            self.assertFalse(result['supplies'])
            self.assertIn('Conflicting/malformed', result['error'])

    def test_electrical_path_and_inventory_bounds(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            dt, node = self.electrical_fixture(root)
            of = root/'sys/bus/platform/devices/4520000.ufs/of_node'
            of.unlink()
            of.symlink_to(root, target_is_directory=True)
            self.assertFalse(c.electrical_evidence(root)['supplies'])
            of.unlink()
            of.symlink_to(node, target_is_directory=True)
            with patch.object(c.os, 'walk', return_value=iter([(str(dt), [], [])]*4097)):
                self.assertIn('DT traversal bound', c.electrical_evidence(root)['error'])
            for index in range(2, 257):
                (root/('sys/class/regulator/regulator.'+str(index))).mkdir()
            self.assertIn('Regulator inventory bound', c.electrical_evidence(root)['error'])

    def test_inventory_identity(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            ctrl = root/'sys/devices/platform/soc/4520000.ufs'
            dev = ctrl/'host0/target0:0:0/0:0:0:0'
            dev.mkdir(parents=True)
            (ctrl/'driver').symlink_to(root/'sys/bus/platform/drivers/sunxi-ufs-pltfm')
            block = root/'sys/class/block/sda'
            block.mkdir(parents=True)
            (block/'device').symlink_to(dev)
            (block/'dev').write_text('8:0')
            (block/'size').write_text('250000000')
            part = root/'sys/class/block/sda1'
            part.mkdir()
            (part/'device').symlink_to(dev)
            (part/'partition').write_text('1')
            usb = root/'sys/class/block/sdb'
            usb.mkdir()
            (usb/'device').symlink_to(root/'sys/devices/platform/usb/0:0:0:0')
            inv = c.ufs_blocks(root)
            self.assertEqual(list(inv), ['sda'])
            self.assertEqual(inv['sda']['attributes']['dev'], '8:0')
            (ctrl/'driver').unlink()
            (ctrl/'driver').symlink_to(root/'sys/bus/platform/drivers/unknown-driver')
            self.assertFalse(c.ufs_blocks(root))

    def test_readonly_default_and_archive(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root/'sys/class/block').mkdir(parents=True)
            node = root/'sys/firmware/devicetree/base/soc/ufs@4520000'
            node.mkdir(parents=True)
            (node/'reg').write_bytes(bytes.fromhex('00000000045200000000000000001000'))
            (node/'status').write_bytes(b'okay\0')
            value = c.collect(root)
            self.assertFalse(value['blocks'])
            self.assertFalse(value['raw_device_writes'])
            self.assertFalse(value['raw_device_reads'])
            self.assertFalse(value['complete_ufs_driver_or_speed_acceptance'])
            self.assertEqual(value['device_tree']['soc/ufs@4520000']['status']['hex'], '6f6b617900')
            a, b = c.save(value, root), c.save(value, root)
            self.assertNotEqual(a, b)
            for p in [a, b]:
                with tarfile.open(p) as tar:
                    self.assertEqual(len(tar.getmembers()), 2)
                    data = tar.extractfile(p.stem.replace('.tar', '')+'/report.json').read()
                    checksum = tar.extractfile(p.stem.replace('.tar', '')+'/SHA256SUMS').read().decode()
                    self.assertTrue(checksum.startswith(hashlib.sha256(data).hexdigest()))
                    self.assertEqual(json.loads(data), value)

    def test_benchmark_refusal_before_open(self):
        for path in ['/dev/mmcblk0', '/dev/sda1', '/dev/nvme0n1', '/tmp/sda',
                     '/dev/../dev/sda', '/dev/sda;echo X', '/dev/sda']:
            with self.subTest(path=path), self.assertRaises(ValueError):
                c.benchmark(path, {})
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)/'big'
            p.write_bytes(b'x'*257)
            self.assertEqual(c.small(p, 256), {'error':'read bound exceeded'})

    def test_benchmark_read_only_and_short_read(self):
        inventory = {'sda': {'attributes': {'dev':'8:0', 'size':'250000000'}}}
        info = SimpleNamespace(st_mode=c.stat.S_IFBLK, st_rdev=c.os.makedev(8, 0))
        def read(fd, buffers, offset):
            self.assertEqual(fd, 123)
            self.assertGreaterEqual(offset, 32*c.BOUND)
            return c.BOUND
        with patch.object(Path, 'is_symlink', return_value=False), \
             patch.object(Path, 'stat', return_value=info), \
             patch.object(c.os, 'open', return_value=123) as opened, \
             patch.object(c.os, 'fstat', return_value=info), \
             patch.object(c.os, 'preadv', side_effect=read) as reads, \
             patch.object(c.os, 'close') as closed:
            result = c.benchmark('/dev/sda', inventory)
            self.assertEqual(result['bytes'], 64*c.BOUND)
            self.assertFalse(result['raw_device_writes'])
            self.assertEqual(reads.call_count, 64)
            flags = opened.call_args.args[1]
            self.assertEqual(flags & c.os.O_ACCMODE, c.os.O_RDONLY)
            self.assertTrue(flags & c.os.O_DIRECT)
            self.assertFalse(flags & (c.os.O_TRUNC | c.os.O_CREAT))
            closed.assert_called_once_with(123)
            reads.reset_mock(side_effect=True)
            reads.return_value = c.BOUND-1
            with self.assertRaises(OSError):
                c.benchmark('/dev/sda', inventory)
            self.assertEqual(reads.call_count, 1)


if __name__ == '__main__':
    unittest.main()
