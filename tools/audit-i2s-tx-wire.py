#!/usr/bin/env python3
"""Read-only TX capture audit: explicit probe map, standard I2S S16/32 slots.

Requires numpy. Emits aggregates only, never writes/plays reconstructed audio.
No automatic channel/edge/offset search. Probe assignment is an explicit input.
"""
import argparse
import collections
import configparser
import hashlib
import json
import re
import zipfile

import numpy as np


def decode_window(a, rate, bclk, lrclk, din):
    if rate < 24e6 or len({bclk, lrclk, din}) != 3:
        raise ValueError('require >=24 MHz and three distinct probe channels')
    if any(c < 0 or c > 7 for c in (bclk, lrclk, din)):
        raise ValueError('probe outside D0..D7')
    edges = np.flatnonzero((~a[:-1] & a[1:]) & (1 << bclk)) + 1
    bounds = np.flatnonzero((a[:-1] ^ a[1:]) & (1 << lrclk)) + 1
    if len(edges) < 64 or len(bounds) < 3:
        raise ValueError('insufficient active clocks')
    loc = np.searchsorted(edges, bounds)
    counts = np.diff(loc)
    if np.any(counts != 32):
        raise ValueError('not 32 BCLK rising edges per LRCLK half-frame')
    # One-bit delay; select exactly the first 16 MSBs. No alternate phase search.
    ix = loc[:-1, None] + np.arange(1, 17)
    bits = ((a[edges[ix]] >> din) & 1).astype(np.int32)
    signed = bits @ (1 << np.arange(15, -1, -1))
    signed = np.where(signed & 0x8000, signed - 65536, signed)
    channels = (a[bounds[:-1]] >> lrclk) & 1
    starts = np.flatnonzero((channels[:-1] == 0) & (channels[1:] == 1))
    pairs = np.column_stack((signed[starts], signed[starts + 1])).astype('<i2')
    if len(pairs) < 49:
        raise ValueError('insufficient complete stereo frames')
    def stats(s):
        v = s.astype(float)
        return dict(nonzero=int(np.count_nonzero(s)), minimum=int(s.min()),
                    maximum=int(s.max()), rms=float(np.sqrt(np.mean(v*v))))
    # Compare to the actual driver's 1 kHz phase accumulator and sine table.
    table = np.array([0,3196,6269,9102,11585,13622,15136,16069,16384,
        16069,15136,13622,11585,9102,6269,3196,0,-3196,-6269,-9102,
        -11585,-13622,-15136,-16069,-16384,-16069,-15136,-13622,-11585,
        -9102,-6269,-3196])
    step = (1000 << 32) // 48000
    reference = table[(((np.arange(48000, 48048, dtype=np.uint64)*step)
                         & 0xffffffff) >> 27).astype(int)]
    pattern = np.arange(len(pairs)) % 48
    # Only waveform phase alignment is searched, never serial decoding settings.
    tone = any(np.array_equal(pairs[:, 0], reference[(pattern+k) % 48])
               for k in range(48))
    falling = bounds[((a[bounds] >> lrclk) & 1) == 0]
    periods = np.diff(edges)
    return dict(decode='rising edge / one-bit delay / first S16 MSBs / 32-bit slots',
        bclk_hz=float(rate*(len(edges)-1)/(edges[-1]-edges[0])),
        lrclk_hz=float(rate*(len(falling)-1)/(falling[-1]-falling[0])),
        bclk_period_samples=dict(collections.Counter(map(int, periods))),
        clocks_per_half_frame=dict(collections.Counter(map(int, counts))),
        frames=len(pairs), left=stats(pairs[:, 0]), right=stats(pairs[:, 1]),
        left_equals_right=bool(np.array_equal(pairs[:, 0], pairs[:, 1])),
        repeats_every_48_frames=bool(np.array_equal(pairs[48:], pairs[:-48])),
        exact_driver_1khz_s16_pattern=tone,
        pcm_sha256=hashlib.sha256(pairs.tobytes()).hexdigest())


def inspect(path, bclk, lrclk, din):
    with zipfile.ZipFile(path) as z:
        if z.read('version').strip() != b'2':
            raise ValueError('expected sigrok v2')
        cfg = configparser.ConfigParser(interpolation=None)
        cfg.read_string(z.read('metadata').decode())
        dev = cfg['device 1']
        m = re.fullmatch(r'(\d+(?:\.\d+)?)\s*([kMG]?)Hz', dev['samplerate'])
        if not m or int(dev['unitsize']) != 1:
            raise ValueError('unsupported sample metadata')
        rate = float(m[1])*{'':1, 'k':1e3, 'M':1e6, 'G':1e9}[m[2]]
        if rate < 24e6:
            raise ValueError('undersampled; require >=24 MHz')
        prefix = dev['capturefile']+'-'
        chunks = sorted([i for i in z.infolist() if i.filename.startswith(prefix)
            and i.filename[len(prefix):].isdigit()],
            key=lambda i: int(i.filename[len(prefix):]))
        if not chunks or sum(i.file_size for i in chunks) > 256*1024*1024:
            raise ValueError('missing/oversized capture')
        a = np.frombuffer(b''.join(z.read(i) for i in chunks), dtype=np.uint8)
    edges = np.flatnonzero((a[:-1] ^ a[1:]) & (1 << bclk)) + 1
    if len(edges) < 64:
        raise ValueError('no sufficient clock activity')
    windows = []
    for fraction in (.25, .5, .75):
        center = int(edges[0] + fraction*(edges[-1]-edges[0]))
        first, last = max(0, center-500000), min(len(a), center+500000)
        windows.append(dict(first_sample=first, last_sample=last,
            **decode_window(a[first:last], rate, bclk, lrclk, din)))
    with open(path, 'rb') as f:
        digest = hashlib.file_digest(f, 'sha256').hexdigest()
    return dict(capture_sha256=digest, samplerate_hz=rate, samples=len(a),
        channel_assignment_explicit=dict(bclk=bclk, lrclk=lrclk, din=din),
        windows=windows, boundary='digital data only; not analog voltage/power or acoustic acceptance')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('capture')
    for name in ('bclk', 'lrclk', 'din'):
        p.add_argument('--'+name, type=int, choices=range(8), required=True)
    args = p.parse_args()
    print(json.dumps(inspect(args.capture, args.bclk, args.lrclk, args.din), indent=2))
