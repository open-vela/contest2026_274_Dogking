#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Host orchestration tests with a fake timeout/runner. NEVER calls the NPU."""
import subprocess
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).parent / "npu-voice/test-linux-voice.sh"
MOCK = r'''
source "$1"
fixture=$2
behavior=$3
environment_log() { printf 'mock environment\n' > "$work/environment.log"; }
preflight() { printf 'MOCK preflight; NOT hardware proof\n'; }
dmesg() { printf 'mock kernel log\n'; }
timeout() {
  printf '%s\n' "$*" >> "$fixture/calls"
  if [[ "$behavior" == timeout ]]; then return 124; fi
  if [[ "$behavior" == missing-marker ]]; then return 0; fi
  local model=$5 ni=1 no=1
  case "$model" in
    *encoder_float*) ni=39; no=39 ;;
    *joiner_float*) ni=2 ;;
  esac
  [[ "$behavior" != counts ]] || ni=38
  printf 'inputs=%s outputs=%s\n' "$ni" "$no"
  if [[ ${6:-} == --zero ]]; then
    mkdir "$7"
    if [[ "$behavior" == different && "$7" == *-b ]]; then
      printf changed > "$7/output0.bin"
    else
      printf stable > "$7/output0.bin"
    fi
    local changed=6
    [[ "$behavior" != unchanged ]] || changed=0
    printf 'run-status=0 wall-us=123\n'
    printf 'output[0] bytes=6 raw-crc32=1234abcd changed=%s status=0\n' "$changed"
  fi
  printf 'reference-status=0; verify this exit status and every output hash\n'
}
if [[ "$behavior" == tee-error ]]; then
  tee() { cat > "$1"; return 1; }
fi
if [[ "$behavior" == env-only ]]; then
  main --env-only --bundle-dir "$fixture"
else
  main --run --npu-idle-confirmed --bundle-dir "$fixture"
fi
'''


class LinuxVoiceScriptTests(unittest.TestCase):
    def run_mock(self, behavior):
        with tempfile.TemporaryDirectory(prefix="voice-script-") as folder:
            path = Path(folder)
            result = subprocess.run(["bash", "-c", MOCK, "--", str(SCRIPT.resolve()), folder, behavior],
                                    text=True, capture_output=True, timeout=15)
            calls = (path / "calls").read_text().splitlines() if (path / "calls").exists() else []
            summaries = list(path.glob("results/run-*/SUMMARY.txt"))
            self.assertEqual(len(summaries), 1, result.stdout + result.stderr)
            summary = summaries[0].read_text()
            self.assertEqual(len(list(path.glob("results/voice-report-*.tar.gz"))), 1)
            self.assertIn("NOT KWS/ASR/TTS accuracy", summary)
            return result, calls, summary

    def test_success(self):
        result, calls, summary = self.run_mock("success")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(calls), 6)
        self.assertIn("result=PASS_INTERFACE_AND_ZERO_REPEAT_ONLY", summary)

    def test_env_only(self):
        result, calls, summary = self.run_mock("env-only")
        self.assertEqual(result.returncode, 0)
        self.assertEqual(calls, [])
        self.assertIn("hardware_started=0", summary)

    def test_failure_stops_and_archives(self):
        for behavior, count in [("timeout", 1), ("missing-marker", 1), ("counts", 1),
                                ("unchanged", 5), ("different", 6), ("tee-error", 1)]:
            with self.subTest(behavior=behavior):
                result, calls, summary = self.run_mock(behavior)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(len(calls), count)
                self.assertIn("result=FAILED", summary)
                if behavior == "timeout":
                    self.assertEqual(result.returncode, 124)
                    self.assertIn("cold reboot", result.stderr)

    def test_explicit_consent_and_help(self):
        for args, expected in [(["--run"], 2), (["--help"], 0), (["--bad"], 2)]:
            result = subprocess.run(["bash", str(SCRIPT), *args], capture_output=True, timeout=5)
            self.assertEqual(result.returncode, expected)

    def test_asset_mismatch_and_partial_manifest(self):
        with tempfile.TemporaryDirectory(prefix="voice-assets-") as folder:
            check = r'''
source "$1"
base=$2
work=$2
printf known > "$base/file"
asset_lock() { printf '%064d file\n' 0; }
verify_assets
'''
            result = subprocess.run(["bash", "-c", check, "--", str(SCRIPT.resolve()), folder], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual((Path(folder) / "file").read_text(), "known")
            # Missing second asset must fail, not verify just the first prefix.
            check = check.replace("printf '%064d file\\n' 0;", "printf '%s file\\n' \"$(sha256sum \"$base/file\" | cut -d' ' -f1)\"; printf '%064d missing\\n' 0;")
            result = subprocess.run(["bash", "-c", check, "--", str(SCRIPT.resolve()), folder], capture_output=True)
            self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
