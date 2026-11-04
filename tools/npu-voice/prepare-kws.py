#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Prepare private host fbank fixtures; no hardware, network or vendor API."""
import argparse
import hashlib
import json
import shutil
import struct
import subprocess
import wave
from pathlib import Path


def prepare(audio, exporter, output, scale="normalized"):
    with wave.open(str(audio), "rb") as stream:
        if (stream.getnchannels(), stream.getsampwidth(), stream.getframerate(), stream.getcomptype()) != (1, 2, 16000, "NONE"):
            raise ValueError("Expected uncompressed mono S16/16k WAV")
        if not 1600 <= stream.getnframes() <= 128000:
            raise ValueError("Expected 0.1..8 seconds; do not truncate silently")
        pcm = stream.readframes(stream.getnframes())
        if len(pcm) != stream.getnframes() * 2:
            raise ValueError("Short WAV")
    output.mkdir()
    manifest = {"schema": 1, "frontend": "v153 voice_fbank / Kaldi native fbank; host CPU",
                "audio_source_name": audio.name, "source_sha256": hashlib.sha256(audio.read_bytes()).hexdigest(),
                "exporter_sha256": hashlib.sha256(exporter.read_bytes()).hexdigest(),
                "normalization": "S16 / 32768" if scale == "normalized" else "S16 PCM units (diagnostic)",
                "tail_silence_samples": 5120,
                "segment_frames": 29, "offset_frames": 16, "encoder_frames": 4,
                "cases": {}}
    for name, content in [("speech", pcm), ("silence", bytes(len(pcm)))]:
        folder = output / name
        folder.mkdir()
        if name == "speech":
            shutil.copyfile(audio, folder / "source.wav")
        raw = folder / "padded-s16le.pcm"
        raw.write_bytes(content + bytes(5120 * 2))
        features = folder / "features.f32"
        command = [str(exporter.resolve()), str(raw.resolve()), str(features.resolve())]
        if scale == "pcm-units":
            command.append("--pcm-units")
        completed = subprocess.run(command,
                                   capture_output=True, text=True, timeout=30, check=True)
        (folder / "frontend.log").write_text(completed.stdout + completed.stderr)
        data = features.read_bytes()
        if len(data) % 320:
            raise ValueError("Feature bytes not N x 80 x FP32")
        frames = len(data) // 320
        chunks = (frames - 29) // 16 + 1
        if not 2 <= chunks <= 50:
            raise ValueError("Unsupported chunk count")
        manifest["cases"][name] = {"frames": frames, "chunks": chunks, "original_samples": len(content) // 2,
                                    "feature_bytes": len(data), "feature_sha256": hashlib.sha256(data).hexdigest(),
                                    "pcm_sha256": hashlib.sha256(raw.read_bytes()).hexdigest()}
        print(name, "frames=", frames, "chunks=", chunks)
    with (output / "manifest.json").open("x") as stream:
        json.dump(manifest, stream, indent=2, sort_keys=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("audio", type=Path)
    parser.add_argument("exporter", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--scale", choices=["normalized", "pcm-units"], default="normalized")
    args = parser.parse_args()
    prepare(args.audio, args.exporter, args.output, args.scale)
