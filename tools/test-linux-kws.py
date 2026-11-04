#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Host/mock-only state/decoder sequencing, never a vendor or target run."""
import importlib.util
import json
import struct
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("stream_kws", TOOLS / "npu-voice/stream-kws.py")
SK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SK)


def desc(size, dims):
    return {"bytes": size, "format": 0, "quant": 0, "dims": dims}


def contract():
    states = [desc(4, [1]) for _ in range(38)]
    return {"encoder": {"input": [desc(9280, [80, 29, 1])] + states,
                        "output": [desc(5120, [320, 4, 1])] + states},
            "decoder": {"input": [desc(8, [2, 1])], "output": [desc(1280, [320, 1])]},
            "joiner": {"input": [desc(1280, [320, 1]), desc(1280, [320, 1])],
                       "output": [desc(1052, [263, 1])]}}


class FakeRunner:
    def __init__(self, failure=None, best=3):
        self.inputs = {}
        self.failure, self.best = failure, best
        self.calls = 0
        self.closed = False

    def close(self):
        self.closed = True

    def invoke(self, tag, name, inputs=None):
        self.calls += 1
        if inputs is None:
            return None
        self.inputs[tag] = list(inputs)
        if self.failure == tag:
            raise ValueError("Injected failure")
        if name == "encoder":
            output = [struct.pack("<1280f", *[SK.floats(inputs[0])[0] / 512] * 1280)]
            output += [struct.pack("<f", SK.floats(data)[0] + (8 if i == 38 else 1))
                       for i, data in enumerate(inputs[1:], 1)]
            if self.failure == "bad-repeat" and "repeat" in tag:
                output[1] = struct.pack("<f", 99)
            return output
        if name == "decoder":
            return [bytes(1280)]
        result = [-1.0] * 263
        result[self.best] = 1.0
        return [struct.pack("<263f", *result)]


class KwsTests(unittest.TestCase):
    def run_case(self, folder, policy="feature-offset", runner=None):
        root = Path(folder)
        (root / "fixtures/speech").mkdir(parents=True)
        data = struct.pack("<3600f", *[0.5] * 3600)  # 45 x 80 = 2 chunks
        (root / "fixtures/speech/features.f32").write_bytes(data)
        runner = runner or FakeRunner()
        tokens = {i: str(i) for i in range(263)}
        record = {"feature_bytes": len(data), "chunks": 2}
        result = SK.stream_case(root, root, runner, contract(), record, "speech", tokens, policy)
        return result, runner

    def test_native_state_feedback_and_feature_scalar(self):
        with tempfile.TemporaryDirectory() as folder:
            result, runner = self.run_case(folder)
            self.assertEqual(SK.floats(runner.inputs["speech-c000-encoder"][1]), (0.0,))
            self.assertEqual(SK.floats(runner.inputs["speech-c001-encoder"][1]), (1.0,))
            self.assertEqual(SK.floats(runner.inputs["speech-c001-encoder"][38]), (16.0,))
            self.assertEqual(runner.inputs["speech-c000-encoder"], runner.inputs["speech-first-encoder-repeat"])
            self.assertEqual(result["processed_lens"][1]["output"], 24)
            self.assertEqual(result["token_ids"], [3] * 8)
            # A repeated nonblank is not CTC-collapsed; RNNT token context shifts.
            self.assertEqual(struct.unpack("<2f", runner.inputs["speech-c000-f1-decoder"][0]), (0, 3))
            self.assertEqual(struct.unpack("<2f", runner.inputs["speech-c000-f2-decoder"][0]), (3, 3))

    def test_feedback_scalar_policy(self):
        with tempfile.TemporaryDirectory() as folder:
            result, runner = self.run_case(folder, "feedback")
            self.assertEqual(SK.floats(runner.inputs["speech-c001-encoder"][38]), (8.0,))
            self.assertEqual(result["processed_lens"][1]["output"], 16)

    def test_blank_unknown_do_not_shift(self):
        for best in (0, 2):
            with self.subTest(best=best), tempfile.TemporaryDirectory() as folder:
                result, runner = self.run_case(folder, runner=FakeRunner(best=best))
                self.assertEqual(result["token_ids"], [])
                self.assertEqual(runner.inputs["speech-c001-f3-decoder"], [bytes(8)])

    def test_first_error_stops_before_later_call(self):
        for fail, later in [("bad-repeat", "speech-c000-f0-decoder"),
                            ("speech-c000-f0-joiner", "speech-c000-f1-decoder")]:
            with self.subTest(fail=fail), tempfile.TemporaryDirectory() as folder:
                runner = FakeRunner(failure=fail)
                with self.assertRaises(ValueError):
                    self.run_case(folder, runner=runner)
                self.assertNotIn(later, runner.inputs)

    def test_metadata_requires_exact_records(self):
        text = ("inputs=1 outputs=1\ninput[0] name=x padded-bytes=8 format=0 quant=0 dims=2x1\n"
                "output[0] name=y padded-bytes=1280 format=0 quant=0 dims=320x1\n"
                "reference-status=0; mock\nprocess-exit=0 tee-exit=0\n")
        self.assertEqual(SK.metadata(text), contract()["decoder"])
        with self.assertRaises(ValueError):
            SK.metadata(text + "inputs=1 outputs=1\n")
        for data in (b"a", struct.pack("<f", float("nan")), struct.pack("<f", float("inf"))):
            with self.assertRaises(ValueError):
                SK.floats(data)

    def test_runner_validates_all_39_native_outputs(self):
        contracts = contract()
        for behavior in ("ok", "input38-crc", "output38-crc", "nan38", "missing38", "cleanup", "exit"):
            with self.subTest(behavior=behavior), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                work = root / "work"
                work.mkdir()

                class FakeProcess:
                    def __init__(self, argv, **kwargs):
                        self.pid = 999999
                        source, output = Path(argv[-2]), Path(argv[-1])
                        output.mkdir()
                        descs = contracts["encoder"]
                        lines = ["inputs=39 outputs=39"]
                        for kind in ("input", "output"):
                            for index, desc in enumerate(descs[kind]):
                                lines.append("%s[%d] name=x padded-bytes=%d format=0 quant=0 dims=%s" %
                                             (kind, index, desc["bytes"], "x".join(map(str, desc["dims"]))))
                        for index, desc in enumerate(descs["input"]):
                            data = (source / ("input%d.bin" % index)).read_bytes()
                            value = SK.crc(data) ^ (1 if behavior == "input38-crc" and index == 38 else 0)
                            lines.append("input[%d] bytes=%d raw-crc32=%08x" % (index, len(data), value))
                        lines.append("run-status=0 wall-us=123")
                        for index, desc in enumerate(descs["output"]):
                            data = bytes(desc["bytes"])
                            if behavior == "nan38" and index == 38:
                                data = struct.pack("<f", float("nan"))
                            if not (behavior == "missing38" and index == 38):
                                (output / ("output%d.bin" % index)).write_bytes(data)
                            value = SK.crc(data) ^ (1 if behavior == "output38-crc" and index == 38 else 0)
                            lines.append("output[%d] bytes=%d raw-crc32=%08x changed=%d status=0" %
                                         (index, len(data), value, len(data)))
                        if behavior != "cleanup":
                            lines.append("reference-status=0; mocked\n")
                        kwargs["stdout"].write("\n".join(lines).encode())

                    def wait(self, timeout):
                        return 1 if behavior == "exit" else 0

                runner = SK.Runner(root, work, contracts)
                try:
                    with patch.object(SK.subprocess, "Popen", FakeProcess):
                        inputs = [bytes(item["bytes"]) for item in contracts["encoder"]["input"]]
                        if behavior == "ok":
                            self.assertEqual(len(runner.invoke("test", "encoder", inputs)), 39)
                        else:
                            with self.assertRaises(ValueError):
                                runner.invoke("test", "encoder", inputs)
                finally:
                    runner.close()

    def test_empty_manifest_cannot_skip_asset_validation(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "stream-assets.json").write_text('{"schema":1,"files":{}}')
            with self.assertRaises(ValueError):
                SK.load_assets(root)

    def test_full_two_fixture_dispatch_and_summary(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            work = root / "work"
            work.mkdir()
            records = {}
            for name, value in (("speech", 0.5), ("silence", -15.0)):
                source = root / "fixtures" / name
                source.mkdir(parents=True)
                data = struct.pack("<3600f", *[value] * 3600)
                (source / "features.f32").write_bytes(data)
                records[name] = {"chunks": 2, "feature_bytes": len(data)}
            fixtures = {"cases": records, "audio_source_name": "mock.wav", "source_sha256": "0" * 64}
            tokens = {i: str(i) for i in range(263)}
            runner = FakeRunner()
            with patch.object(SK, "load_assets", return_value=(contract(), fixtures, tokens)), \
                 patch.object(SK, "Runner", return_value=runner):
                SK.run(root, work, "feature-offset")
            summary = json.loads((work / "stream-summary.json").read_text())
            self.assertEqual(summary["result"], "COLLECTED_REAL_KWS_GREEDY_DIAGNOSTIC")
            self.assertEqual(runner.calls, 41)
            self.assertTrue(runner.closed)
            self.assertTrue((work / "speech-greedy.json").exists())
            self.assertTrue((work / "silence-greedy.json").exists())


if __name__ == "__main__":
    unittest.main()
