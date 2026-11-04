# Returned stage 3 audited / all-available-assets batch prepared

2026-10-19. No target board connection or new openvela image in this turn.

## Actual returned data

Original user archive `voice-report-rAyPoGAl.tar.gz` retained, SHA-256
`87f992bde187f88fc5cd6028ea453b4ef4ca0e6b5a3a20b9626e113c693aed84`.
`audit-kws-return.py` read-only replay checks 218 native inference records, all input/output
hashes/CRCs/metadata, 39-output first-chunk repeats, 37 native cache links plus explicit
scalar policy, decoder contexts and encoder/decoder-to-joiner byte links.
Result: `RAW_TRACE_AND_STATE_LINKS_VERIFIED`. Recognition is NOT accepted.

Speech/silence both 48/48 blank greedy frames. Maximum nonblank probability:
speech 0.2518160882536296, silence 0.0010119502482845311.
Native run call min/median/max us: encoder 26409/26713.5/27196;
decoder 471/631.5/1418; joiner 222/340.5/744. Not full-chain/warm throughput.
Native scalar output=input+8 versus previous feature-offset policy 0/16/32.

## Private local package, not redistributed in Git

Final `out/a733-linux-voice-all-v153-20261019-181042-r2.tar.gz`, 9213398 bytes,
SHA-256 `829b375da7abf2950d179a6fdd930cc24ea3381eb8fbf98ab5dee523a8ad0bc8`.
Earlier package, foundations and host build directories retained, not overwritten.
Archive extracted into a new directory and offline batch checks passed. Packaged script
hashes matched current sources, LF verified. Original 7 locked native assets unchanged.

New bounded repeat binary SHA-256
`5c79c59714212697e2ba766b137eba97e11993a5e9c81aa6ed7ada3a4784078e`;
AArch64 with only GLIBC_2.17 requirements (readelf). Built with existing Bullseye sysroot.
Supports 1..20 prepared-network runs, output sentinel reset and complete dumps each time;
used only as a new separate binary. No actual repeat-binary target run yet.

## Real CPU TTS fixtures

Isolated workspace `.hostdeps-voice-batch`, CPU ONNX Runtime 1.23.2, no system-Python change.
Existing Matcha model SHA-256 `ff1cc7f7abdedc4eeec668a4a0dc2e72a0c314c5637d5fce885345b490bd08de`.
Reference text frontend/model/lexicon used, existing reference sources unchanged.
“你好”: 70 kept mel frames; mel SHA-256
`7d58bce5722b928c2d25136554d66f844948998b69270058b7b6f9e8736995a5`.
“测试成功”: 115 kept mel frames; mel SHA-256
`6d739783c5ec0238a3cc0cc830bf278848e5aa6cd06dd82aae86aef90200997d`.
80x200 FP32 native chunks padded -11.4, not KWS fbank. CPU source/runtime/feed/model
provenance retained in the private kit. Baker model noncommercial restriction noted.
This is development-host CPU acoustic execution, not A7Z CPU/NPU acoustic acceptance.

## Verified on host only

- Stage-1 script tests 5 OK; stage-2 tests 5 OK; stage-3 tests 8 OK; batch tests 7 OK.
- Batch cases include CRC/drift/cleanup failure rejection, exact warm/reload comparison,
  24 scenarios all dispatched despite semantic misses, explicit scalar strategies,
  decoder cache origins, WAV crop/no amplification and conservative 75C test stop.
- C ASan/UBSan mock suite: all 39 inputs/outputs, 3 repeats with one prepare/finish,
  zero/bad repeat count rejection and existing error/cleanup tests passed. No NPU calls.
- Wrapper and builder Bash syntax checks passed.

## Target execution pending / limits

One command runs current-asset tests; real hardware/CRC errors stop later calls.
Semantic miss continues collecting other safe scenarios. 24 KWS scenarios use 6 distinct
sample WAVs x 2 explicit feature amplitude conventions x 2 scalar policies; greedy plus
legacy reference keyword rules reported separately, including ignored threshold/+5 bias.
Four-model warm/reload, controlled malformed-input rejection and real-mel vocoder/WAV
collection included. No automatic playback/reset/reboot. All clocks/thermal policy untouched;
75C is a conservative test stopping policy, not a silicon rating.

ASR executable NBG/conversion toolchain not validated, live audio, RTOS NPU ABI, Agent
integration, listening quality and intentionally wedged-hardware reset recovery are not
accepted or fabricated. See `tools/npu-voice/LINUX_VOICE_ALL.md` for full coverage.
