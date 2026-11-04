#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Host-only fixtures and mocked subprocess tests; never calls vendor/NPU APIs."""
import hashlib
import importlib.util
import json
import math
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
HELPER = TOOLS / "npu-voice/joiner-fixtures.py"
SPEC = importlib.util.spec_from_file_location("joiner_fixtures", HELPER)
JF = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(JF)


def base_output():
    return struct.pack("<263f", *[i / 128 - 1 for i in range(263)])


def metadata():
    return ("inputs=2 outputs=1\n"
            "input[0] name=x padded-bytes=1280 format=0 quant=0 dims=320x1\n"
            "input[1] name=y padded-bytes=1280 format=0 quant=0 dims=320x1\n"
            "output[0] name=z padded-bytes=1052 format=0 quant=0 dims=263x1\n")


def fake_run(args):
    model, inputs = args[:2]
    behavior = args[-1]
    print(metadata(), end="")
    if inputs != "--inspect":
        output = Path(args[2])
        output.mkdir()
        data = base_output()
        tensors = [(Path(inputs) / ("input%d.bin" % index)).read_bytes() for index in range(2)]
        offset = sum(abs(v) for v in struct.unpack("<320f", tensors[0])) / 512
        offset += sum(abs(v) for v in struct.unpack("<320f", tensors[1])) / 1024
        if behavior == "stale":
            offset = 0
        if behavior == "repeat" and output.name == "encoder-b":
            offset += 0.5
        if offset:
            data = struct.pack("<263f", *[v + offset for v in struct.unpack("<263f", data)])
        if behavior == "nan":
            data = struct.pack("<f", math.nan) + data[4:]
        if behavior == "baseline":
            data = struct.pack("<263f", *[0.5] * 263)
        (output / "output0.bin").write_bytes(data)
        for index, tensor in enumerate(tensors):
            value = JF.crc(tensor) ^ (1 if behavior == "input-crc" else 0)
            print("input[%d] bytes=1280 raw-crc32=%08x" % (index, value))
        print("run-status=0 wall-us=123")
        value = JF.crc(data) ^ (1 if behavior == "output-crc" else 0)
        print("output[0] bytes=1052 raw-crc32=%08x changed=%d status=0" %
              (value, sum(v != 0xa5 for v in data)))
    print("reference-status=0; mock ONLY")


MOCK = r'''
source "$1/test-linux-joiner.sh"
fixture=$2
test_driver=$3
behavior=$4
environment_log() { printf 'mock environment\n' > "$work/environment.log"; }
preflight() { printf 'MOCK preflight; NOT hardware proof\n'; }
dmesg() { printf 'mock dmesg\n'; }
timeout() {
  printf '%s\n' "$*" >> "$fixture/calls"
  [[ "$behavior" != timeout ]] || return 124
  python3 "$test_driver" --fake-run "${@:5}" "$behavior"
}
main --run --npu-idle-confirmed --bundle-dir "$fixture"
'''


class JoinerTests(unittest.TestCase):
    def test_crc(self):
        value = 0
        for byte in b"123456789":
            value ^= byte
            for _ in range(8):
                value = (value >> 1) ^ (0xedb88320 if value & 1 else 0)
        self.assertEqual(JF.crc(b"123456789"), value)
        self.assertEqual(JF.crc(bytes(1280)), 0)

    def test_fixtures_preserve_and_detect_changes(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            JF.prepare(root)
            JF.fixtures_check(root)
            manifest = json.loads((root / "fixture-manifest.json").read_text())
            self.assertEqual(len(manifest), 8)
            with self.assertRaises(FileExistsError):
                JF.prepare(root)
            path = root / "fixtures/encoder/input0.bin"
            path.write_bytes(bytes(1280))
            with self.assertRaises(ValueError):
                JF.fixtures_check(root)

    def test_contract_rejects_mismatch_duplicate(self):
        valid = metadata() + "reference-status=0; mock\nprocess-exit=0 tee-exit=0\n"
        JF.contract(valid)
        for text in [valid.replace("format=0", "format=1"), valid.replace("quant=0", "quant=1"),
                     valid.replace("320x1", "1x320"), valid + "inputs=2 outputs=1\n",
                     valid.replace("1052", "1056")]:
            with self.assertRaises(ValueError):
                JF.contract(text)

    def mock(self, behavior):
        with tempfile.TemporaryDirectory(prefix="joiner-mock-") as folder:
            root = Path(folder)
            # Only the disposable test helper gets a fake baseline. Production
            # has no environment/CLI bypass for golden checks or device access.
            fake_hash = hashlib.sha256(base_output()).hexdigest()
            (root / "joiner-fixtures.py").write_text(
                HELPER.read_text().replace(JF.ZERO_HASH, fake_hash))
            result = subprocess.run(["bash", "-c", MOCK, "--", str(HELPER.parent), folder,
                                     str(Path(__file__).resolve()), behavior],
                                    text=True, capture_output=True, timeout=20)
            calls = (root / "calls").read_text().splitlines()
            summaries = list(root.glob("results/run-*/SUMMARY.txt"))
            self.assertEqual(len(summaries), 1, result.stdout + result.stderr)
            self.assertEqual(len(list(root.glob("results/voice-report-*.tar.gz"))), 1)
            return result, len(calls), summaries[0].read_text()

    def test_success(self):
        result, calls, summary = self.mock("success")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(calls, 7)
        self.assertIn("PASS_SYNTHETIC_JOINER_ONLY", summary)
        self.assertIn("NOT KWS/ASR/TTS", summary)

    def test_failure_stops(self):
        for behavior, count in [("timeout", 1), ("stale", 3), ("repeat", 6), ("nan", 2),
                                ("input-crc", 2), ("output-crc", 2), ("baseline", 2)]:
            with self.subTest(behavior=behavior):
                result, calls, summary = self.mock(behavior)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(calls, count, result.stdout + result.stderr)
                self.assertIn("result=FAILED", summary)


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--fake-run":
        fake_run(sys.argv[2:])
    else:
        unittest.main()
