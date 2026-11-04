#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Host preparation, no NPU. CPU Matcha fixtures explicitly separated from target."""
import argparse
import hashlib
import importlib.util
import json
import math
import struct
import sys
from pathlib import Path


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def features(reference, exporter, output):
    prep = module("prepare_kws", Path(__file__).with_name("prepare-kws.py"))
    output.mkdir()
    cases = []
    for name, filename, expected in [
        ("wake-full", "wake_nihao_xiaorui_16k.wav", "你好小瑞"),
        ("wake-short", "wake_xiaorui_16k.wav", "小瑞"),
        ("wake-double", "wake_xiaorui_xiaorui_16k.wav", "小瑞小瑞"),
        ("negative-command", "cmd_open_light_16k.wav", ""),
        ("negative-noise", "neg_noise_3s_16k.wav", ""),
        ("negative-silence", "neg_silence_3s_16k.wav", "")]:
        for scale in ("normalized", "pcm-units"):
            tag = name + "-" + scale
            folder = output / tag
            prep.prepare(reference / "samples" / filename, exporter, folder, scale)
            manifest = json.loads((folder / "manifest.json").read_text())
            record = manifest["cases"]["speech"]
            cases.append({"tag": tag, "expected": expected, "scale": scale, "source": filename,
                          "source_sha256": manifest["source_sha256"], "frames": record["frames"],
                          "chunks": record["chunks"], "feature_bytes": record["feature_bytes"],
                          "feature_sha256": record["feature_sha256"],
                          "feature_path": "batch-fixtures/" + tag + "/speech/features.f32"})
    (output / "cases.json").write_text(json.dumps(cases, indent=2, ensure_ascii=False), encoding="utf-8")


def tts(reference, model, output):
    # Isolated CPU ORT dependency on the development host only. No installation
    # or ORT dependency is added to the target or openvela firmware.
    import numpy as np
    import onnxruntime as ort
    ref = module("reference_matcha", reference / "tools/matcha_npu.py")
    options = ort.SessionOptions()
    options.intra_op_num_threads = 4
    options.inter_op_num_threads = 1
    session = ort.InferenceSession(str(model / "model-steps-3.onnx"), options,
                                   providers=["CPUExecutionProvider"])
    ref._ORT_SESS, ref._ORT_PATH = session, str(model / "model-steps-3.onnx")
    tokens = ref.load_tokens(model / "tokens.txt")
    lexicon = ref.load_lexicon(model / "lexicon.txt", tokens)
    output.mkdir()
    records = []
    for utterance, text in enumerate(("你好", "测试成功")):
        sentences = ref.text_to_sentences(text, tokens, lexicon)
        if not sentences:
            raise ValueError("No text frontend output")
        for sentence, ids in enumerate(sentences):
            mel, elapsed = ref.run_acoustic(ids, model / "model-steps-3.onnx")
            if mel.ndim != 3 or mel.shape[:2] != (1, 80) or not 1 <= mel.shape[-1] <= 800:
                raise ValueError("Unsupported acoustic output")
            if not np.isfinite(mel).all():
                raise ValueError("Nonfinite mel")
            # Do not silently truncate 201..224 frames as the reference demo does.
            for start in range(0, mel.shape[-1], 200):
                take = min(200, mel.shape[-1] - start)
                piece = np.full((1, 80, 200), -11.4, dtype=np.float32)
                piece[:, :, :take] = mel[:, :, start:start + take]
                tag = "tts-%d-%d-%d" % (utterance, sentence, start // 200)
                path = output / (tag + ".f32")
                with path.open("xb") as stream:
                    stream.write(piece.astype("<f4").tobytes())
                records.append({"tag": tag, "text": text, "token_ids": ids.tolist(), "mel_frames_kept": take,
                                "path": "tts-fixtures/" + path.name, "sha256": digest(path),
                                "acoustic_cpu_seconds": elapsed})
                print(tag, text, "frames=", take, "CPU seconds=", elapsed, flush=True)
    # Meaningful silence mel for synthesis, not KWS fbank and not all-zero mel.
    floor = output / "silence-floor.f32"
    with floor.open("xb") as stream:
        stream.write(struct.pack("<16000f", *[-11.4] * 16000))
    result = {"ort_version": ort.__version__, "cpu_execution_host": sys.platform,
              "model_sha256": digest(model / "model-steps-3.onnx"),
              "lexicon_sha256": digest(model / "lexicon.txt"), "tokens_sha256": digest(model / "tokens.txt"),
              "source_sha256": digest(reference / "tools/matcha_npu.py"),
              "license": "Baker trained model: non-commercial only; upstream notice retained",
              "npu_acoustic_execution": False, "items": records}
    (output / "manifest.json").write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=["features", "tts"])
    parser.add_argument("reference", type=Path)
    parser.add_argument("tool_or_model", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.mode == "features":
        features(args.reference, args.tool_or_model, args.output)
    else:
        tts(args.reference, args.tool_or_model, args.output)
