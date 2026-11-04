#!/usr/bin/env python3
"""Synthetic metadata audit tests; never claims to decode physical I2S."""
import hashlib
import importlib.util
import sys
import tempfile
import zipfile
from pathlib import Path

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('sigrok_audit', Path(__file__).with_name('check-sigrok-i2s.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
with tempfile.TemporaryDirectory(prefix='i2s-sigrok-test-') as directory:
    for rate, expected in [('20 kHz', False), ('24 MHz', True), ('50 MHz', True)]:
        path = Path(directory) / (rate.replace(' ', '-') + '.sr')
        with zipfile.ZipFile(path, 'w') as archive:
            archive.writestr('version', '2')
            archive.writestr('metadata', '[device 1]\ncapturefile=logic-1\nunitsize=1\nsamplerate=' + rate + '\n')
            archive.writestr('logic-1-2', bytes([2, 3]))
            archive.writestr('logic-1-1', bytes([0, 1]))
        original = hashlib.sha256(path.read_bytes()).hexdigest()
        result = module.inspect(path)
        assert result['meets_recommended_24MHz'] == expected
        assert result['logic_samples'] == 4
        assert result['sampled_channel_statistics_not_bus_frequencies'][0]['transitions'] == 3
        assert result['sampled_channel_statistics_not_bus_frequencies'][1]['transitions'] == 1
        assert hashlib.sha256(path.read_bytes()).hexdigest() == original
print('PASS: 20kHz rejected as undersampled, 24/50MHz gate only, numeric chunk ordering, source unchanged')
