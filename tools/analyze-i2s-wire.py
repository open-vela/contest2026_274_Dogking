#!/usr/bin/env python3
"""Read-only I2S D0=WS/D1=SCK/D2=SD audit. No firmware or input changes.

Decode only the agreed standard: rising-edge sample, one bit delay, 24 valid
bits in 32-bit slots. Do not search alternate edge/offsets for a pleasing result.
Output aggregates/hashes only; raw voice and reconstructed PCM stay local.
"""
import argparse
import array
import bisect
import collections
import configparser
import hashlib
import json
import math
import re
import sys
import wave
import zipfile
from pathlib import Path


def stats(values):
    return {'samples': len(values), 'nonzero': sum(v != 0 for v in values),
            'min': min(values, default=0), 'max': max(values, default=0),
            'rms_s16': math.sqrt(sum(v*v for v in values) / len(values)) if values else 0}


def decode(data, rate):
    if rate < 24_000_000:
        raise ValueError('undersampled for this audit; require >=24 MHz')
    rising = [i for i in range(1, len(data)) if not data[i-1] & 2 and data[i] & 2]
    boundaries = [i for i in range(1, len(data)) if (data[i-1] ^ data[i]) & 1]
    intervals = [rising[i+1] - rising[i] for i in range(len(rising)-1)]
    falls = [i for i in boundaries if not data[i] & 1]
    frame_periods = [b-a for a, b in zip(falls, falls[1:])]
    counts = []
    words = []
    for start, end in zip(boundaries, boundaries[1:]):
        edges = rising[bisect.bisect_left(rising, start):bisect.bisect_left(rising, end)]
        counts.append(len(edges))
        if len(edges) != 32:
            raise ValueError(f'not a complete 32-clock half-frame: {len(edges)}')
        value = 0
        for sample in edges[1:25]:
            value = (value << 1) | ((data[sample] >> 2) & 1)
        if value & 0x800000:
            value -= 1 << 24
        words.append((data[start] & 1, value >> 8))
    pairs = [(words[i][1], words[i+1][1]) for i in range(len(words)-1)
             if words[i][0] == 0 and words[i+1][0] == 1]
    pcm = array.array('h', (v for pair in pairs for v in pair))
    if sys.byteorder != 'little':
        pcm.byteswap()
    packed = pcm.tobytes()
    result = {
        'samplerate_hz': rate, 'capture_samples': len(data), 'capture_seconds': len(data)/rate,
        'channel_map': {'D0': 'WS/LRCLK', 'D1': 'SCK/BCLK', 'D2': 'SD'},
        'decode': 'rising edge / one-bit delay / MSB first / S24 / 32-bit slots',
        'sck_rising_edges': len(rising),
        'sck_period_samples_histogram': dict(sorted(collections.Counter(intervals).items())),
        'sck_hz_from_edge_span': rate*(len(rising)-1)/(rising[-1]-rising[0]) if len(rising)>1 else None,
        'ws_hz_from_falling_edge_span': rate*(len(falls)-1)/(falls[-1]-falls[0]) if len(falls)>1 else None,
        'ws_period_samples_histogram': dict(sorted(collections.Counter(frame_periods).items())),
        'clocks_per_half_frame_histogram': dict(sorted(collections.Counter(counts).items())),
        'complete_stereo_frames': len(pairs),
        'left': stats([p[0] for p in pairs]), 'right': stats([p[1] for p in pairs]),
        'decoded_s16_stereo_sha256': hashlib.sha256(packed).hexdigest(),
    }
    return result, packed


def inspect(path, reference=None):
    path = Path(path)
    with zipfile.ZipFile(path) as archive:
        config = configparser.ConfigParser(interpolation=None)
        config.read_string(archive.read('metadata').decode('utf-8'))
        device = config['device 1']
        m = re.fullmatch(r'(\d+(?:\.\d+)?)\s*([kMG]?)Hz', device['samplerate'])
        if not m or int(device['unitsize']) != 1:
            raise ValueError('unsupported sampling metadata')
        rate = float(m[1]) * {'': 1, 'k': 1e3, 'M': 1e6, 'G': 1e9}[m[2]]
        prefix = device['capturefile'] + '-'
        chunks = [i for i in archive.infolist() if i.filename.startswith(prefix)
                  and i.filename[len(prefix):].isdigit()]
        chunks.sort(key=lambda i: int(i.filename[len(prefix):]))
        if not chunks or sum(i.file_size for i in chunks) > 256*1024*1024:
            raise ValueError('missing/oversized logic data')
        data = b''.join(archive.read(i) for i in chunks)
    result, pcm = decode(data, rate)
    result['capture_sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
    if reference is not None:
        reference = Path(reference)
        with wave.open(str(reference), 'rb') as wav:
            if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) != (2, 2, 48000):
                raise ValueError('reference must be stereo S16 at 48kHz')
            payload = wav.readframes(wav.getnframes())
        offset = payload.find(pcm) if len(pcm) >= 256 and result['left']['nonzero'] >= 16 else -1
        aligned = offset >= 0 and offset % 4 == 0
        result['reference_wav_sha256'] = hashlib.sha256(reference.read_bytes()).hexdigest()
        result['entire_decoded_segment_matches_reference'] = aligned
        result['matching_reference_offset_seconds'] = offset/4/48000 if aligned else None
        result['reference_note'] = ('exact contiguous PCM match' if aligned else
                                    'no exact match; captures may be from different moments/operations')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--reference-wav', type=Path)
    args = parser.parse_args()
    print(json.dumps(inspect(args.capture, args.reference_wav), indent=2, ensure_ascii=False))
