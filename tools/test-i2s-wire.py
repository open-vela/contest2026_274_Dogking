#!/usr/bin/env python3
"""Synthetic I2S protocol audit: framing, bit delay, sign and PCM matching."""
import array
import hashlib
import importlib.util
import sys
import tempfile
import wave
import zipfile
from pathlib import Path

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('wire_audit', Path(__file__).with_name('analyze-i2s-wire.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
data = bytearray()
pairs = [(1000 + 100*i, -500 - 50*i) for i in range(80)]
for pair in pairs:
    for slot, value in enumerate(pair):
        raw = (value << 8) & 0xffffff
        bits = [0] + [(raw >> i) & 1 for i in range(23, -1, -1)] + [0]*7
        for bit in bits:
            for tick in range(8):
                data.append(slot | ((tick >= 4) << 1) | (bit << 2))
result, pcm = module.decode(data, 24_000_000)
assert result['clocks_per_half_frame_histogram'] == {32: 158}
assert result['complete_stereo_frames'] == 78
assert result['sck_hz_from_edge_span'] == 3_000_000
assert result['ws_hz_from_falling_edge_span'] == 46_875
expected = array.array('h', (v for p in pairs[1:79] for v in p))
if sys.byteorder != 'little':
    expected.byteswap()
assert pcm == expected.tobytes()
try:
    module.decode(data, 20_000)
except ValueError:
    pass
else:
    raise AssertionError('must reject undersampling')
with tempfile.TemporaryDirectory(prefix='i2s-wire-test-') as directory:
    root = Path(directory)
    source, reference = root/'capture.sr', root/'reference.wav'
    with zipfile.ZipFile(source, 'w') as archive:
        archive.writestr('version', '2')
        archive.writestr('metadata', '[device 1]\nunitsize=1\ncapturefile=logic-1\nsamplerate=24 MHz\n')
        archive.writestr('logic-1-1', data)
    whole = array.array('h', (v for p in pairs for v in p))
    if sys.byteorder != 'little':
        whole.byteswap()
    with wave.open(str(reference), 'wb') as wav:
        wav.setparams((2, 2, 48000, len(pairs), 'NONE', 'not compressed'))
        wav.writeframes(whole.tobytes())
    before = [hashlib.sha256(p.read_bytes()).hexdigest() for p in (source, reference)]
    result = module.inspect(source, reference)
    assert result['entire_decoded_segment_matches_reference']
    assert result['matching_reference_offset_seconds'] == 1/48000
    assert before == [hashlib.sha256(p.read_bytes()).hexdigest() for p in (source, reference)]
print('PASS: synthetic 32-bit slots/S24 one-bit delay, signed left/right, measured frequencies, reject undersampling, exact PCM match, originals unchanged')
