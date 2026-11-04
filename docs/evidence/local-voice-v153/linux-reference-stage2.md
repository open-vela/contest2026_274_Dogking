# Linux reference / stage 2 host preparation

2026-10-19. No new firmware and no stage-2 board inference performed here.

## Returned stage-1 evidence, independently checked

- Archive: `voice-report-tCO4OwuR.tar.gz` (workspace private evidence directory).
- SHA-256: `dd481ad1387fbe167aec984d35bede9e89135085f57e0751be0d278f9d4f9e82`.
- All 7 asset verification records OK; 4 interface queries exited/cleaned up successfully.
- Joiner zero results: 1052 bytes each, byte-identical; 263 finite FP32 values.
- SHA-256: `b2a50b97a684a67b8957eb54674aea44f5a9d395101d7c175654f9316161fff3`.
- Raw runner CRC: `7529f5ca`; finite range -4.97265625 .. 2.75, argmax 198.
- Run calls: 253 us / 269 us, not complete ASR or warm performance.
- New validator checked the original returned binary plus log and matched this CRC/hash.
- Original archives, returned logs, binaries and passed images were not modified.

## Stage-2 preparation

Private local archive (under ignored `out/`, no remote upload):
`a733-linux-voice-dynamic-v153-20261019-170122.tar.gz`, 7725601 bytes.
SHA-256: `ad67135d06ff97b1fbfb747e34b9e8dfb003e795647e0feb9bd9aa0066c196b2`.

Includes the same seven locked binary/library/model assets, new scripts, standard-library
Python validator, and instructions. All seven staged asset hashes checked, script copies
matched source and use LF. Archive file listing checked. Vendor assets remain outside Git.

Host validation (WSL, not target):

```text
bash -n tools/npu-voice/test-linux-voice.sh    PASS
bash -n tools/npu-voice/test-linux-joiner.sh   PASS
python3 tools/test-linux-voice-script.py     Ran 5 tests / OK
python3 tools/test-linux-joiner.py           Ran 5 tests / OK
joiner-fixtures.py output RETURNED_COPY zero-a   PASS (original real stage-1 bytes)
```

Subcases include timeout/no later call, missing cleanup, wrong counts/type/quant/layout,
changed input fixture, bad input/output CRC, NaN, wrong zero baseline, stale output,
repeat mismatch and tee failure. All hardware-facing calls in host tests are mocked.

Stage-2 successful result must be `PASS_SYNTHETIC_JOINER_ONLY`, not speech recognition,
encoder state feedback, TTS, warm benchmarking or openvela acceptance. Actual stage-2
board execution is pending user return. See `tools/npu-voice/LINUX_JOINER_CHECK.md`.
