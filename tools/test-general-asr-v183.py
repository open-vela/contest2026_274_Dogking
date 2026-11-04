#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Host safety/contract checks, no NPU, network, packages or reset."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('native_asr', Path(__file__).parent / 'npu-voice/asr-native-v183.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
TEMP_PARENT = Path(__file__).resolve().parents[1] / 'out'


class NativeTests(unittest.TestCase):
    def test_nonfinite(self):
        for x in (float('nan'), float('inf'), -float('inf')):
            with self.assertRaisesRegex(ValueError, 'nonfinite'):
                m.floats(struct.pack('<f', x))

    def test_metadata_rejects_missing_status_and_dimensions(self):
        text = ('inputs=1 outputs=1\ninput[0] name=x padded-bytes=8 format=0 quant=0 dims=2x1\n'
                'output[0] name=x padded-bytes=8 format=0 quant=0 dims=2x1\nreference-status=0; clean\n')
        self.assertEqual(m.metadata(text)['input'][0]['bytes'], 8)
        for changed in (text.replace('reference-status=0;', ''),
                        text.replace('dims=2x1', 'dims=4x1'), text + 'inputs=1 outputs=1\n'):
            with self.assertRaises(ValueError):
                m.metadata(changed)

    def test_exclusive_output(self):
        with tempfile.TemporaryDirectory(dir=TEMP_PARENT) as d:
            p = Path(d) / 'evidence'
            m.write(p, b'first')
            with self.assertRaises(FileExistsError):
                m.write(p, b'second')
            self.assertEqual(m.read(p), b'first')

    def test_keeps_all_native_states_and_not_cpu_oracle_states(self):
        with tempfile.TemporaryDirectory(dir=TEMP_PARENT) as d:
            root = Path(d)
            fixture = root / 'fixtures/speech'
            fixture.mkdir(parents=True)
            feature = bytes(199 * 80 * 4)
            m.write(fixture / 'features.f32', feature)
            native = m.Native.__new__(m.Native)
            native.base = native.work = root
            native.contract = {'encoder': {'input': [{'bytes': 32960}] + [{'bytes': 4}] * 35}}
            states = [struct.pack('<f', i + 1) for i in range(35)]
            encodes = []
            def invoke(role, inputs, label):
                if role == 'decoder':
                    return [bytes(2048)]
                if role == 'encoder':
                    encodes.append(inputs[1:])
                    return [bytes(49152), *states]
                return [bytes(6254 * 4)]
            native.call = invoke
            result = native.case(dict(case='speech', feature_bytes=len(feature),
                                      feature_sha256=m.sha(feature), real_frames=192,
                                      text='', token_ids=[], frame_positions=[]), {0: '<blk>'})
            self.assertEqual(encodes[0], [bytes(4)] * 35)
            self.assertEqual(encodes[1], encodes[0])  # identical-input repeat
            self.assertEqual(encodes[2], states)      # next block uses native outputs
            self.assertEqual(result['retained_native_state_count'], 35)
            self.assertTrue(result['cpu_text_equal'])
            self.assertFalse(result['full_voice_chain'])


if __name__ == '__main__':
    unittest.main()
