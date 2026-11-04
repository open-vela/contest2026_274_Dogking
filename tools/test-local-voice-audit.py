#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import importlib.util
import struct
import tempfile
import unittest
import wave
from pathlib import Path

spec = importlib.util.spec_from_file_location("audit", Path(__file__).with_name("audit-a733-local-voice.py"))
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class TestAudit(unittest.TestCase):
    def test_headers(self):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory) / "a.nb"
            for version in (0x10020, 0x20000):
                p.write_bytes(struct.pack("<III", 0x4e4d5056, version, 0x1000003b) + bytes(52))
                report = audit.nbg(p)
                self.assertFalse(report["execution_proven"])
                self.assertEqual(report["existing_metadata_probe_accepts_version"], version == 0x10020)
            for data in (b"", bytes(64), struct.pack("<III", 0x4e4d5056, 0x20000, 0xdeadbeef)+bytes(52)):
                p.write_bytes(data)
                with self.assertRaises(ValueError):
                    audit.nbg(p)

    def test_wav(self):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory) / "a.wav"
            with wave.open(str(p), "wb") as dest:
                dest.setparams((1, 2, 16000, 0, "NONE", "not compressed"))
                dest.writeframes(bytes(3200))
            self.assertEqual(audit.wav_info(p)["seconds"], 0.1)
            p.write_bytes(p.read_bytes()[:-1])
            with self.assertRaises(ValueError):
                audit.wav_info(p)

    def test_missing_never_ready(self):
        with tempfile.TemporaryDirectory() as directory:
            report = audit.audit(Path(directory), Path(directory), Path(__file__).resolve().parents[1])
            self.assertFalse(report["openvela_voice_ready"])
            self.assertEqual(len(report["artifacts"]), 5)
            self.assertTrue(all(not item["valid"] for item in report["artifacts"]))
            self.assertIn("Model Zoo lacks examples/zipformer", report["blockers"])
            self.assertFalse(report["frontend"]["npu_inference"])
            self.assertEqual(report["asr_assets"]["expected_encoder_inputs"], 36)

    def test_source_does_not_satisfy_compiled_model_gate(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "version").write_text("1.1.0\n")
            (root / "examples/zipformer/model").mkdir(parents=True)
            (root / "examples/zipformer/model/tokens.txt").write_text("<blk> 0\n")
            (root / "examples/dataset/zipformer").mkdir(parents=True)
            report = audit.audit(root, root, Path(__file__).resolve().parents[1])
            self.assertTrue(report["zoo"]["zipformer"])
            self.assertTrue(report["asr_assets"]["calibration_present"])
            for role in ("encoder", "decoder", "joiner"):
                self.assertEqual(report["asr_assets"]["compiled_models"][role], [])
            self.assertFalse(report["openvela_voice_ready"])


if __name__ == "__main__":
    unittest.main()
