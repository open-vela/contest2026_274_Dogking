#!/usr/bin/env python3
"""Read-only sigrok logic capture audit, NOT an I2S/PCM decoder.

Do not infer actual bus frequencies from undersampled transitions. Channel
mapping below is the mapping confirmed by the user for I2S-1.sr.
"""
import argparse
import configparser
import hashlib
import json
import re
import zipfile
from pathlib import Path


def inspect(path):
    path = Path(path)
    with zipfile.ZipFile(path) as archive:
        if archive.read('version').strip() != b'2':
            raise ValueError('only sigrok v2 is supported')
        config = configparser.ConfigParser(interpolation=None)
        config.read_string(archive.read('metadata').decode('utf-8'))
        device = config['device 1']
        rate = re.fullmatch(r'(\d+(?:\.\d+)?)\s*([kMG]?)Hz', device['samplerate'].strip())
        if not rate:
            raise ValueError('unsupported samplerate encoding')
        sample_rate = float(rate[1]) * {'': 1, 'k': 1e3, 'M': 1e6, 'G': 1e9}[rate[2]]
        if sample_rate <= 0 or int(device['unitsize']) != 1:
            raise ValueError('expected positive rate and 8-bit packed logic samples')
        prefix = device['capturefile'] + '-'
        chunks = [i for i in archive.infolist()
                  if i.filename.startswith(prefix) and i.filename[len(prefix):].isdigit()]
        chunks.sort(key=lambda i: int(i.filename[len(prefix):]))
        if not chunks or sum(i.file_size for i in chunks) > 256 * 1024 * 1024:
            raise ValueError('missing chunks or capture exceeds 256 MiB audit limit')
        data = b''.join(archive.read(i) for i in chunks)
    sufficient = sample_rate >= 24_000_000
    return {
        'capture': path.name, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
        'metadata_samplerate': device['samplerate'], 'samplerate_hz': sample_rate,
        'logic_samples': len(data), 'duration_seconds': len(data) / sample_rate,
        'expected_bus': {'lrclk_hz': 48000, 'bclk_hz': 3072000, 'slots': 2, 'slot_bits': 32},
        'samples_per_expected_bclk_period': sample_rate / 3072000,
        'meets_bclk_nyquist_only': sample_rate > 2 * 3072000,
        'meets_recommended_24MHz': sufficient,
        'interpretation': ('sampling rate passes this preliminary gate; timing/PCM still require decoding'
                           if sufficient else 'UNDERSAMPLED: do not decode PCM or infer actual clock frequency'),
        'user_confirmed_channel_map': {'D0': 'LRCLK', 'D1': 'BCLK', 'D2': 'SD'},
        'sampled_channel_statistics_not_bus_frequencies': [
            {'channel': c, 'high_samples': sum((v >> c) & 1 for v in data),
             'transitions': sum(bool((a ^ b) & (1 << c)) for a, b in zip(data, data[1:]))}
            for c in range(8)
        ],
    }


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    args = parser.parse_args()
    print(json.dumps(inspect(args.capture), indent=2, ensure_ascii=False))
