# Linux reference: stage 2 accepted / stage 3 prepared

2026-10-19. Existing passed images, cards and test archives retained.

## Actual stage-2 return

Private original: `Linux测试-Npu测试文件/voice-report-EviWxFpk.tar.gz`, 30596 bytes.
SHA-256 `9dbdc8cac232d8cfffb981266e7d9a84ca7284085bd8115e3aba40139c742b04`.
Independent local `joiner-fixtures.py compare` passed on the returned raw inputs/outputs.
Zero returned to the first baseline; repeat encoder input was byte-identical. Both input
branches independently affect the joiner, with distinct zero/encoder/decoder/both results.
Run calls: 350/248/245/232/262/271 us; output CRCs respectively
`7529f5ca/b7af8f1e/49280ac2/3a60b4ed/b7af8f1e/7529f5ca`.
This is synthetic joiner sensitivity/repeatability, not recognition accuracy or warm performance.

## Stage-3 private kit

`out/a733-linux-kws-real-v153-20261019-173256.tar.gz`.
SHA-256 `df6670b8dd99e89eee5b5fdcd11d2f79fe56b2e20035e395424939117371f035`.
Build script: `tools/npu-voice/build-linux-kws-kit.sh NEW_ABSOLUTE_OUTPUT_DIRECTORY`.
No NPU/board access or network during this build. All native binary/model/library hashes
unchanged from the first tests. Fixed interface contract extracted from user's actual
stage-1 metadata, checked at every native call. Private vendor assets remain outside Git.

CPU fixture source: `wake_nihao_xiaorui_16k.wav`, 28416 samples, SHA-256
`466795b62e022e3575bdb849476decdae716a7a99ebe56b0c23527ba28191d83`.
Matched silence plus original speech each receive 5120 zero samples; v153 application
Kaldi frontend exports 210 x 80 FP32 values, 12 overlapping chunks each.
Speech feature SHA-256 `60ff8d4388e9b4debf5b1261fb242801d819c6aa48a42003fd71b8758073f460`.
Silence feature SHA-256 `0bfef8f4b293f36f578c2c70a90b70f9ba6101a8e17802cf3f318a465977f0ef`.
Host exporter SHA-256 `66e92e4e3b71290e01a3679a77a66461f6beef6fb9ed6c57401d7fbbbb32fd87`.
Archive extracted to a separate new check directory; offline asset/fixture/FP32 contract
validation passed there. All packaged script hashes matched source and used LF.

## Host verification, not target acceptance

- Stage-1 script mock regression: 5 tests OK.
- Stage-2 fixture/script mock regression: 5 tests OK.
- Stage-3 state/greedy/metadata/native tensor mock tests: 8 tests OK.
- `bash -n` stage-3 wrapper and builder: passed.
- Existing real Kaldi frontend suite: sanitizers, no-exceptions, RTOS-logger modes all passed;
  batch/stream exact, reference normalized parity, silence floor, tones, short/10s bounds/errors.
- No stage-3 native network execution has occurred here; actual board return pending.

Runtime intent: all 38 native encoder states preserved; FP32 scalar overridden by feature
offset 0/16/32, matching reference default. All original scalar outputs retained. Greedy
one-token/frame diagnostics are not the reference keyword-biased beam/trie or Chinese ASR.
Output label is `COLLECTED_REAL_KWS_GREEDY_DIAGNOSTIC`, not end-to-end acceptance.
See `tools/npu-voice/LINUX_KWS_CHECK.md` for full limits, deadlines and data handling.
