#!/usr/bin/env python3
"""Synthetic file regression only; not microphone acceptance."""
import array
import hashlib
import importlib.util
import sys
import tempfile
import wave
from pathlib import Path

spec = importlib.util.spec_from_file_location('ics_analysis', Path(__file__).with_name('analyze-ics43434.py'))
module = importlib.util.module_from_spec(spec)
sys.dont_write_bytecode = True
spec.loader.exec_module(module)
with tempfile.TemporaryDirectory(prefix='ics43434-analysis-') as directory:
    root = Path(directory)
    pcm = array.array('h')
    for i in range(4800):
        pcm.extend((0 if i < 2400 else (1000 if i % 2 else -1000), -11437))
    if sys.byteorder != 'little':
        pcm.byteswap()
    path = root / 'stereo.wav'
    with wave.open(str(path), 'wb') as wav:
        wav.setparams((2, 2, 48000, 4800, 'NONE', 'not compressed'))
        wav.writeframes(pcm.tobytes())
    before = hashlib.sha256(path.read_bytes()).hexdigest()
    report = module.analyze(path, root / 'result', 0.1)
    assert report['frames'] == 4800 and report['file_bytes'] == 19244
    assert report['channel_metrics'][0]['zero_samples'] == 2400
    assert report['channel_metrics'][1]['mean'] == -11437
    assert report['channel_metrics'][1]['ac_rms'] == 0
    for channel, name in enumerate(('left', 'right')):
        with wave.open(str(root / 'result' / (name + '.wav')), 'rb') as wav:
            assert wav.getnchannels() == 1 and wav.getnframes() == 4800
            out = array.array('h', wav.readframes(4800))
            if sys.byteorder != 'little':
                out.byteswap()
            if channel == 0:
                expected = array.array('h', (0 if i < 2400 else
                                             (1000 if i % 2 else -1000)
                                             for i in range(4800)))
                assert out == expected
            else:
                assert all(x == -11437 for x in out)
    try:
        module.analyze(path, root / 'result', 0.1)
    except FileExistsError:
        pass
    else:
        raise AssertionError('must refuse output overwrite')
    try:
        module.analyze(path, root / 'wrong-duration', 5)
    except ValueError:
        pass
    else:
        raise AssertionError('must reject mislabeled duration')
    assert not (root / 'wrong-duration').exists()
    assert hashlib.sha256(path.read_bytes()).hexdigest() == before
print('PASS: synthetic stereo split, constant-channel AC RMS=0, duration gate, source unchanged, no overwrite')
