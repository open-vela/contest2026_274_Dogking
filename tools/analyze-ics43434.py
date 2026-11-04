#!/usr/bin/env python3
"""Read-only WAV analysis; export channels and metrics into a NEW directory.

No automatic 'microphone passed' decision: listen to speech and compare known
quiet/speech/clap intervals. Constant/nonzero streams are not acoustic proof.
Only Python's standard library is needed.
"""
import argparse
import array
import csv
import hashlib
import json
import math
import sys
import wave
from pathlib import Path


def metrics(samples):
    count = len(samples)
    total = sum(samples)
    squares = sum(x * x for x in samples)
    mean = total / count if count else 0
    return {
        'samples': count, 'min': min(samples, default=0),
        'max': max(samples, default=0), 'mean': mean,
        'rms': math.sqrt(squares / count) if count else 0,
        'ac_rms': math.sqrt(max(0, squares / count - mean * mean)) if count else 0,
        'zero_samples': samples.count(0),
        'clipped_samples': sum(x == -32768 or x == 32767 for x in samples),
        'adjacent_equal': sum(a == b for a, b in zip(samples, samples[1:])),
    }


def analyze(path, output, expected_seconds):
    path, output = Path(path).resolve(), Path(output).resolve()
    with wave.open(str(path), 'rb') as wav:
        channels, width, rate, frames = (wav.getnchannels(), wav.getsampwidth(),
                                          wav.getframerate(), wav.getnframes())
        if wav.getcomptype() != 'NONE' or width != 2 or channels not in (1, 2):
            raise ValueError('expected uncompressed S16 mono/stereo WAV')
        if rate != 48000:
            raise ValueError('expected 48000 Hz; do not relabel a misclocked capture')
        data = wav.readframes(frames)
        if len(data) != frames * channels * width:
            raise ValueError('truncated PCM payload')
    if expected_seconds is not None and frames != round(expected_seconds * rate):
        raise ValueError(f'WAV frame count mismatch: {frames} vs {expected_seconds}s at {rate} Hz')
    samples = array.array('h', data)
    if sys.byteorder != 'little':
        samples.byteswap()
    report = {
        'input': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
        'file_bytes': path.stat().st_size, 'rate': rate, 'channels': channels,
        'sample_bits': 16, 'frames': frames, 'seconds': frames / rate,
        'acceptance': 'UNVERIFIED: metrics and file sizes do not prove intelligible speech',
        'channel_metrics': [], 'windows': [],
    }
    channel_pcm = []
    for channel in range(channels):
        pcm = samples[channel::channels]
        channel_pcm.append(pcm)
        name = ('left', 'right')[channel] if channels == 2 else 'mono'
        report['channel_metrics'].append({'channel': name, **metrics(pcm)})
        for start in range(0, frames, rate // 10):
            report['windows'].append({
                'channel': name, 'start_s': start / rate,
                **metrics(pcm[start:start + rate // 10]),
            })
    # Never overwrite an older analysis or the source recording.
    output.mkdir(parents=True, exist_ok=False)
    for channel, pcm in enumerate(channel_pcm):
        name = ('left', 'right')[channel] if channels == 2 else 'mono'
        if sys.byteorder != 'little':
            pcm.byteswap()
        with wave.open(str(output / f'{name}.wav'), 'wb') as wav:
            wav.setparams((1, 2, rate, frames, 'NONE', 'not compressed'))
            wav.writeframes(pcm.tobytes())
    (output / 'analysis.json').write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
    with (output / 'windows-100ms.csv').open('w', newline='', encoding='utf-8') as csvfile:
        writer = csv.DictWriter(csvfile, fieldnames=report['windows'][0].keys() if report['windows'] else ['channel'])
        writer.writeheader()
        writer.writerows(report['windows'])
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('wav', type=Path)
    parser.add_argument('--output-dir', required=True, type=Path)
    parser.add_argument('--expected-seconds', type=float, default=5)
    args = parser.parse_args()
    if not math.isfinite(args.expected_seconds) or args.expected_seconds <= 0:
        parser.error('--expected-seconds must be positive and finite')
    report = analyze(args.wav, args.output_dir, args.expected_seconds)
    print(json.dumps({k: v for k, v in report.items() if k != 'windows'}, indent=2, ensure_ascii=False))


if __name__ == '__main__':
    main()
