#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Host mocks only: keyword legacy gates, multi-run CRC and WAV evidence."""
import importlib.util
import struct
import tempfile
import time
import unittest
import wave
import json
from pathlib import Path
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("voice_batch", Path(__file__).parent / "npu-voice/run-voice-batch.py")
VB = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VB)


def descriptor(size, dims):
    return {"bytes": size, "dims": dims, "format": 0, "quant": 0}


JOINER = {"input": [descriptor(1280, [320, 1]), descriptor(1280, [320, 1])],
          "output": [descriptor(1052, [263, 1])]}


class BatchTests(unittest.TestCase):
    def test_conservative_thermal_cap_preserves_sample_and_stops(self):
        class Sensor:
            name = "temp"
            def read_text(self):
                return "75000\n"
            def __str__(self):
                return "/sys/class/thermal/mock/temp"
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            with patch.object(VB.Path, "glob", return_value=[Sensor()]):
                with self.assertRaisesRegex(ValueError, "75C"):
                    VB.environment(root, "mock")
            data = json.loads((root / "batch-environment.jsonl").read_text())
            self.assertEqual(data["software_temp_cap_mc"], 75000)

    def test_semantic_miss_keeps_collecting_state_matrix(self):
        for policy, expected_scalar in (("feature-offset", 16), ("feedback", 8)):
            with self.subTest(policy=policy), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                (root / "features.f32").write_bytes(struct.pack("<3600f", *[0.5] * 3600))
                (root / "keywords.txt").write_text("a b c d :2.5 #0.15 @wake")
                states = [descriptor(4, [1]) for _ in range(38)]
                contract = {"encoder": {"input": [descriptor(9280, [80, 29, 1])] + states,
                                         "output": [descriptor(5120, [320, 4, 1])] + states}}
                batch = VB.Batch.__new__(VB.Batch)
                batch.base = batch.work = root
                batch.contract, batch.decoder_cache = contract, {}
                class FakeRunner:
                    def __init__(self):
                        self.seen = {}
                        self.calls = 0
                    def invoke(self, tag, name, inputs):
                        self.calls += 1
                        self.seen[tag] = inputs.copy()
                        if name == "encoder":
                            return [bytes(5120)] + [struct.pack("<f", VB.SK.floats(data)[0] + (8 if i == 38 else 1))
                                                     for i, data in enumerate(inputs[1:], 1)]
                        if name == "decoder":
                            return [bytes(1280)]
                        return [struct.pack("<263f", 10, *[-10.0] * 262)]
                batch.runner = FakeRunner()
                tokens = {i: str(i) for i in range(263)}
                tokens.update({3: "a", 4: "b", 5: "c", 6: "d"})
                case = {"tag": "fixture-normalized", "feature_path": "features.f32", "feature_bytes": 14400,
                        "chunks": 2, "scale": "normalized", "expected": "wake", "source": "mock.wav"}
                with patch.object(VB, "environment"):
                    result = batch.scenario(case, policy, tokens)
                self.assertFalse(result["fixture_classification_ok"])
                self.assertEqual(len(result["frames"]), 16)
                self.assertEqual(len(batch.decoder_cache), 1)
                self.assertEqual(batch.runner.calls, 12)
                following = batch.runner.seen["fixture-normalized-" + policy + "-c001-encoder"]
                self.assertEqual(VB.SK.floats(following[38]), (expected_scalar,))
                self.assertEqual(VB.SK.floats(following[1]), (1.0,))

    def test_legacy_keyword_rules_and_unused_threshold_visible(self):
        graph = VB.KR.Keywords("a b c d :2.5 #0.99 @wake", {"a": 3, "b": 4, "c": 5, "d": 6})
        hit = None
        for frame, token in enumerate((3, 4, 5, 6)):
            values = [-10.0] * 263
            values[0], values[token] = 1.0, 2.0
            chosen, hit = graph.consume(values, frame)
            self.assertEqual(chosen, token)
        self.assertEqual(hit["phrase"], "wake")
        self.assertEqual(hit["legacy_threshold_ignored"], 0.99)
        self.assertEqual(graph.context, [0, 0])

    def test_silence_cannot_start_keyword(self):
        graph = VB.KR.Keywords("a b c d :2.5 #0.15 @wake", {"a": 3, "b": 4, "c": 5, "d": 6})
        for frame in range(48):
            values = [-10.0] * 263
            values[0] = 8.0
            chosen, hit = graph.consume(values, frame)
            self.assertEqual(chosen, 0)
            self.assertIsNone(hit)
        self.assertEqual(graph.context, [0, 0])

    def test_warm_repeat_and_reload_validation(self):
        for behavior in ("ok", "crc", "drift", "missing-cleanup", "exit"):
            with self.subTest(behavior=behavior), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                batch = VB.Batch.__new__(VB.Batch)
                batch.base = batch.work = root
                batch.contract = {"joiner": JOINER}

                class FakeRunner:
                    deadline = time.monotonic() + 100
                    calls = 0
                    def invoke(self, tag, name, inputs):
                        return [bytes(1052)]

                batch.runner = FakeRunner()

                class FakeProcess:
                    def __init__(self, argv, **kwargs):
                        source, output = Path(argv[-4]), Path(argv[-3])
                        repeats = int(argv[-1])
                        output.mkdir()
                        lines = ["inputs=2 outputs=1"]
                        for kind in ("input", "output"):
                            for index, desc in enumerate(JOINER[kind]):
                                lines.append("%s[%d] name=x padded-bytes=%d format=0 quant=0 dims=%s" %
                                             (kind, index, desc["bytes"], "x".join(map(str, desc["dims"]))))
                        for index in range(2):
                            data = (source / ("input%d.bin" % index)).read_bytes()
                            lines.append("input[%d] bytes=1280 raw-crc32=%08x" % (index, VB.SK.crc(data)))
                        for step in range(repeats):
                            lines += ["repeat-index=%d total=%d" % (step, repeats), "run-status=0 wall-us=%d" % (100 + step)]
                            folder = output / ("step%03d" % step)
                            folder.mkdir()
                            data = bytes(1052)
                            if behavior == "drift" and step == 1:
                                data = struct.pack("<f", 1) + data[4:]
                            (folder / "output0.bin").write_bytes(data)
                            value = VB.SK.crc(data) ^ (1 if behavior == "crc" and step == 1 else 0)
                            lines.append("output[0] bytes=1052 raw-crc32=%08x changed=%d status=0" %
                                         (value, sum(v != 0xa5 for v in data)))
                        if behavior != "missing-cleanup":
                            lines.append("reference-status=0; mocked")
                        kwargs["stdout"].write(("\n".join(lines) + "\n").encode())
                    def wait(self, timeout):
                        return 124 if behavior == "exit" else 0

                with patch.object(VB.subprocess, "Popen", FakeProcess):
                    if behavior == "ok":
                        result = batch.warm("joiner", [bytes(1280), bytes(1280)], 3)
                        self.assertEqual(result["raw_native_call_us"], [100, 101, 102])
                        self.assertEqual(result["warm_median_us"], 101.5)
                    else:
                        with self.assertRaises(ValueError):
                            batch.warm("joiner", [bytes(1280), bytes(1280)], 3)

    def test_vocoder_wav_crop_no_amplification(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "tts-fixtures").mkdir()
            for name, value in (("silence-floor", -11.4), ("tts-0-0-0", 0.1)):
                (root / "tts-fixtures" / (name + ".f32")).write_bytes(struct.pack("<16000f", *[value] * 16000))
            batch = VB.Batch.__new__(VB.Batch)
            batch.base = batch.work = root
            class FakeRunner:
                def invoke(self, tag, name, inputs):
                    value = VB.SK.floats(inputs[0])[0] / 1000
                    return [struct.pack("<51200f", *[value] * 51200)]
            batch.runner = FakeRunner()
            items = {"items": [{"tag": "tts-0-0-0", "path": "tts-fixtures/tts-0-0-0.f32",
                                "mel_frames_kept": 70, "text": "fixture"}]}
            results = batch.vocoder(items)
            self.assertEqual(results[1]["safety_attenuation_only"], 1)
            with wave.open(str(root / "vocoder-tts-0-0-0.wav"), "rb") as stream:
                self.assertEqual(stream.getnframes(), 70 * 256)
                self.assertEqual(stream.getframerate(), 22050)
            self.assertEqual(results[1]["semantic_audio_acceptance"], "LISTENING_PENDING")

    def test_all_24_scenarios_dispatch_even_when_semantic_checks_fail(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "features.f32").write_bytes(bytes(9280))
            (root / "tts-fixtures").mkdir()
            (root / "tts-fixtures/silence-floor.f32").write_bytes(bytes(64000))
            contracts = {"encoder": {"input": [descriptor(9280, [80, 29, 1])] + [descriptor(4, [1])] * 38}}
            cases = [{"tag": "case-%d-%s" % (i, scale), "scale": scale, "feature_path": "features.f32"}
                     for i in range(6) for scale in ("normalized", "pcm-units")]
            class FakeRunner:
                calls = 0
                def invoke(self, *args):
                    pass
                def close(self):
                    pass
            class FakeBatch:
                def __init__(self, *args):
                    self.runner, self.decoder_cache = FakeRunner(), {}
                    self.dispatched = []
                def negative_input(self):
                    return {"result": "MOCK"}
                def warm(self, name, inputs):
                    return {"output_sha256": [VB.SK._jf.ZERO_HASH]}
                def vocoder(self, tts):
                    return []
                def scenario(self, case, policy, tokens):
                    return {"scenario": case["tag"] + policy, "scale": case["scale"], "lens_policy": policy,
                            "expected": "wake", "hits": [], "fixture_classification_ok": False,
                            "exact_phrase_observed": False, "greedy_phonemes": ""}
            with patch.object(VB, "check", return_value=(contracts, {}, cases, {})), patch.object(VB, "Batch", FakeBatch):
                VB.run(root, root)
            summary = json.loads((root / "batch-summary.json").read_text())
            self.assertEqual(len(summary["matrix"]), 24)
            self.assertFalse(summary["end_to_end_accepted"])
            self.assertTrue(all(item["fixture_count"] == 6 for item in summary["configs"].values()))


if __name__ == "__main__":
    unittest.main()
