#!/usr/bin/env python3
"""Measure matrix WAV contents and compare them with the board transcript.

Requires NumPy. Does not perform listening or speech recognition.
"""
import argparse
import csv
import hashlib
import json
import math
import re
import wave
from pathlib import Path

import numpy as np


def spectrum(samples, rate):
    centered = samples.astype(np.float64) - samples.mean()
    if not np.any(centered):
        return {"voice_band_percent": None, "band_4_10k_percent": None,
                "dominant_hz": None, "centroid_hz": None}
    size = 4096
    window = np.hanning(size)
    power = np.zeros(size // 2 + 1)
    for start in range(0, len(centered) - size + 1, size // 2):
        power += abs(np.fft.rfft(centered[start:start + size] * window)) ** 2
    frequency = np.fft.rfftfreq(size, 1 / rate)
    total = power.sum()
    return {
        "voice_band_percent": float(100 * power[(frequency >= 300) & (frequency <= 3400)].sum() / total),
        "band_4_10k_percent": float(100 * power[(frequency >= 4000) & (frequency <= 10000)].sum() / total),
        "dominant_hz": float(frequency[power.argmax()]),
        "centroid_hz": float((frequency * power).sum() / total),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--audio-dir', type=Path, required=True)
    parser.add_argument('--board-log', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    log_bytes = args.board_log.read_bytes()
    log = log_bytes.decode('utf-8-sig')
    blocks = re.findall(r'echo capturecfg=(\d,\d,\d,\d+)(.*?)(?=nsh> echo capturecfg=|\Z)', log, re.S)
    board = {}
    for cfg, block in blocks:
        filename = re.search(r'capture-output: /data/(\S+)', block)[1]
        board[filename] = (cfg, block)
    if len(board) != 8:
        raise ValueError(f'Expected 8 board reports, found {len(board)}')
    rows = []
    for filename in sorted(board):
        path = args.audio_dir / filename
        with wave.open(str(path), 'rb') as wav:
            if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getcomptype()) != (1, 2, 48000, 'NONE'):
                raise ValueError(f'Unexpected WAV format: {filename}')
            rate = wav.getframerate()
            samples = np.frombuffer(wav.readframes(wav.getnframes()), dtype='<i2').astype(np.int64)
        square = int(np.dot(samples, samples))
        transitions = int(np.count_nonzero((samples[:-1] < 0) != (samples[1:] < 0)))
        zero = int(np.count_nonzero(samples == 0))
        cfg, block = board[filename]
        slot, edge, pull, duration = map(int, cfg.split(','))
        actual_format = int(re.search(r'capture-config:.*?fmt0=([0-9a-f]+)', block)[1], 16)
        actual_map = int(re.search(r'capture-config:.*?rxmap3=([0-9a-f]+)', block)[1], 16)
        actual_pull = int(re.search(r'capture-config:.*?pb-pull=([0-9a-f]+)', block)[1], 16)
        if (actual_format != (0x1f57 | (edge << 7)) or actual_map != slot or
                ((actual_pull >> 16) & 3) != pull or len(samples) != duration * rate // 1000):
            raise ValueError(f'Configuration/readback mismatch: {filename}')
        if duration != 3000:
            raise ValueError('This analysis expects the documented 3-second matrix')
        expected = {'samples': len(samples), 'capture-min': int(samples.min()),
                    'capture-max': int(samples.max()), 'sum-square': square,
                    'zero': zero, 'sign-transitions': transitions,
                    'capture-nonzero': len(samples) - zero}
        checks = {key: int(re.search(r'(?<![\w-])' + re.escape(key) + r'=(-?\d+)', block)[1]) == value
                  for key, value in expected.items()}
        if not all(checks.values()):
            raise ValueError(f'WAV/transcript mismatch: {filename}: {checks}')
        unique, counts = np.unique(samples, return_counts=True)
        centered = samples.astype(np.float64) - samples.mean()
        row = {
            'file': filename, 'config': cfg, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
            'bytes': path.stat().st_size, 'rate': rate, 'samples': len(samples),
            'seconds': len(samples) / rate, 'minimum': int(samples.min()), 'maximum': int(samples.max()),
            'dc_mean': float(samples.mean()), 'rms': math.sqrt(square / len(samples)),
            'ac_rms': float(np.sqrt(np.mean(centered ** 2))),
            'sum_square': square, 'zero': zero, 'sign_transitions': transitions,
            'sign_transition_percent': 100 * transitions / (len(samples) - 1),
            'unique_values': len(unique),
            'adjacent_repeat_percent': float(100 * np.mean(samples[:-1] == samples[1:])),
            'top_values': [[int(unique[i]), int(counts[i])] for i in np.argsort(counts)[-8:][::-1]],
            'one_second_rms': [float(np.sqrt(np.mean(part.astype(np.float64) ** 2)))
                               for part in np.split(samples, 3)],
            'one_second_ac_rms': [float(np.std(part)) for part in np.split(samples, 3)],
            'rms_100ms': [float(np.sqrt(np.mean(part.astype(np.float64) ** 2)))
                          for part in np.split(samples, 30)],
            'wave_matches_board_stats': True,
            'board': {key: int(re.search(r'(?<![\w-])' + key + r'=(\d+)', block)[1])
                      for key in ('elapsed', 'target-rx', 'consumed-rx', 'hw-rx',
                                  'fifo-peak', 'raw-low55', 'raw-lowaa', 'committed', 'status')},
            **spectrum(samples, rate),
        }
        rows.append(row)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    report = {'board_log_sha256': hashlib.sha256(log_bytes).hexdigest(),
              'spectral_method': 'Mean-subtracted Hann-window power, 4096 samples, 50% overlap; no listening/ASR',
              'rows': rows}
    (args.output_dir / 'audio-analysis.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    fields = ('file', 'config', 'sha256', 'samples', 'rms', 'ac_rms', 'dc_mean',
              'minimum', 'maximum', 'unique_values', 'sign_transition_percent',
              'adjacent_repeat_percent', 'voice_band_percent', 'band_4_10k_percent',
              'dominant_hz', 'wave_matches_board_stats')
    with (args.output_dir / 'audio-analysis.csv').open('w', newline='', encoding='utf-8') as output:
        writer = csv.DictWriter(output, fieldnames=fields, extrasaction='ignore')
        writer.writeheader()
        writer.writerows(rows)
    for row in rows:
        print(f"{row['file']}: RMS={row['rms']:.2f}, AC={row['ac_rms']:.2f}, "
              f"DC={row['dc_mean']:.2f}, unique={row['unique_values']}, "
              f"1s_RMS={','.join(f'{x:.2f}' for x in row['one_second_rms'])}, "
              f"4-10k={row['band_4_10k_percent']}, log_match=PASS")


if __name__ == '__main__':
    main()
