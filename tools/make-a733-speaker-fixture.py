#!/usr/bin/env python3
"""Create a NEW two-second 16kHz mono PCM16 test WAV; never overwrite files."""
import argparse
import array
import math
import sys
import wave
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('output', type=Path)
args = parser.parse_args()
rate, frames = 16000, 32000
pcm = array.array('h', (round(8192 * min(1, i/1600, (frames-1-i)/1600)
                             * math.sin(2*math.pi*440*i/rate))
                        for i in range(frames)))
if sys.byteorder != 'little':
    pcm.byteswap()
with args.output.open('xb') as output:
    with wave.open(output, 'wb') as wav:
        wav.setparams((1, 2, rate, frames, 'NONE', 'not compressed'))
        wav.writeframes(pcm.tobytes())
print(f'Created synthetic test only: {args.output} (64044 bytes, 16kHz mono, 2sec, -12dBFS peak)')
