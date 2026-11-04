# v208-r2 sealed evidence — NOT board/FPS acceptance

Source 97f1426ed20824d743123a25af0c078e989e08d8. Final image SHA256:
c7bf742a705d2866f6b31214744da3f70496336dd7857d967d30f7fc44c41207.
summary.json binds 100 payloads and the ten external sealed binaries/
configuration/mode/scripts, source identity and independent Windows hash.

Includes 20 host groups, true ARM CRC/gate execution under QEMU, old/new
decoder byte comparison, concurrent queue faults/stop/backpressure, native
ARM objects/stack, full ELF/ISA/local O2 command audit, SDK restoration,
image readback/GPT/unchanged non-root spans and project-local QEMU package
versions/hashes. QEMU alignment/GPR/atomic flags are test-harness-only.

The board pass is v207's separate user log. v208-r2 needs boot, six-head
equality, serial/pipe A/B, clearing, stop and soak tests. No 30 FPS,
three-stage concurrency or physical clock measurement is claimed.
The single guarded RAW DMA pool and hardware execution source stay intact.

Initial r1 was rejected at final compiler-flag auditing (CMake scope).
It remains unsubmitted, not overwritten. r2 fixes scope and reruns gates.
See [test/rebuild/recovery procedure](../../a733/IMX214_V208_CPU_PIPELINE_TEST.md).
