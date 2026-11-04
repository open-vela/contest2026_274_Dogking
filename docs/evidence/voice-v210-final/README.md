# v210 sealed voice candidate — NOT board acceptance

180 byte-exact evidence payloads plus summary bind firmware source
e400da868c59ce447b6f17e410535057b33e0f19, 12 voice regressions,
22 camera regressions, 16 native voice units, final ELF/layout/ISA, 24
constructors, 297-path SDK restore, image readback and independent Windows SHA.

Image SHA256:
53816b0e9f6a111c6346ef477a8eaebaa59273770360fe916fba8654bb1024e1.
Kernel SHA256:
677d11dcef344c171b0f6b7ff8e1aa28ea98f64a3d306772d719bf658376de95.

ASR regression replays 216 real Linux vendor-library capture records against
CPU production frontend/state code with a mock NPU. TTS executes real CPU
acoustic inference against existing mel references, with a stub vocoder NPU.
QEMU executes actual ARM CRC code; it is not the physical A733. Negative tests
intentionally print errors/retained owners; their assertions and exit status
are the test contract, not a claim that production errors are ignored.

The final image adds three hash-locked standard WAVs; 49 inherited private
assets are independently read back unchanged. Models/WAVs/executables are not
committed in this evidence tree. No physical disk/UFS writes, reset/retry,
new hardware acceptance, recognition accuracy, end-to-end acoustic latency or
3–5 second achievement is claimed. v209 is preserved and deferred for board test.

See docs/a733/VOICE_V210_RELEASE.md and VOICE_V210_TEST.md for exact rebuild,
test order, recovery scope and known measurement limitations.
