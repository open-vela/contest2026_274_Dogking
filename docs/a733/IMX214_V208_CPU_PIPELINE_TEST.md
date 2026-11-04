# v208: qualified ARM CRC, exact input overwrite, CPU placement and RGB pipeline

2026-10-29. Performance candidate, NOT a measured 30 FPS release.
v207 source/image/checkpoint are retained. Its [user short test](../evidence/camera-v207-board-20261029/README.md)
passed same-input legacy/cold/warm six-head equality and 154 frames/inferences:
warm 5.158 FPS, error/cleanup/fallback zero. This is not a soak test.

## Changes and contracts

* IEEE reflected raw CRC32, polynomial 0xedb88320, unchanged seed/chunk/count
  semantics. CRC32X/B are GPR-only optional opcodes, not CRC32C or global ISA.
  An EL1 ID_AA64ISAR0 feature check, singleton thread affinity and MPIDR match
  are required. Each translation-unit/core dispatch validates 640 seed,
  alignment and fused-count cases before activation. Unknown/unpinned/failed
  gates retain slicing-by-eight. Do not change worker affinity externally
  while active. All six entire outputs, guards and post-run LOAD checks stay.
* Only live dynamic INPUT whose entire region is exactly overwritten can
  omit copying its template. Its immutable CPU snapshot template still has
  matching region/input CRCs qualified; partial inputs/padding, COMMAND,
  non-live, static inputs use the original path. Exact file read, size/CRC,
  input clean and hardware submission gates remain; short reads cannot run.
  Device-written/unknown mutable LOADs still reload; no extra weight leases.
* Camera prepare/infer-display workers request single A76 CPUs 7/6 and read
  MIDR/MPIDR back. Failure logs an explicit fallback; no global scheduler,
  IRQ affinity, priority, PLL, voltage or DVFS writes. CPU PLL-B/mux and RTC
  are sampled read-only twice only after this running A76 is confirmed.
  Frequency is a vendor-register estimate, NOT a physical measurement.
  CNTFRQ is the timer frequency, explicitly NOT CPU frequency. Unknown or
  unstable muxes report zero/unknown, never a guessed GHz.
* Pixel code fills only letterbox padding (including odd bars) because all
  active pixels are rewritten. Two per-source files use local O2, with no
  fast-math or global ISA changes. YOLO class-major reads are tiled in 64
  cells, retaining class/tie/NaN order, sigmoid/DFL, insertion order and NMS.
* New opt-in `live-pipe`: camera+CPU preparation overlaps synchronous
  NPU+LVGL/SPI consumption. This is TWO stages, not yet three independently
  parallel capture/infer/display engines. Display and NPU stay serialized.
  The accepted single permanent guarded RAW DMA buffer is UNCHANGED.
  Extra application RGB storage is one 1,228,800-byte slot plus a tone plan
  and a 32KiB producer pthread stack. Each slot has its own sequence, CRC,
  transform and timings; FREE -> WRITING -> READY -> READING -> FREE under
  a mutex. No overwriting/skipping READY slots. No MQ pointer handoff.
  Input file/NPU/display finish before slot release; copied RAW is owned
  exclusively by preparation. A stalled consumer produces bounded backpressure.
  Join precedes freeing either slot/RAW/plans. Join failure quarantines and
  retains these buffers until reboot rather than freeing uncertain owners.
  Cooperative stop discards at most two unfinished slots, explicitly counted.
  Any capture failure still requires reboot, not retry/reset.

CPU PLL reference: local vendor `configs/linux-5.15/sun60iw2p1.dtsi` CPU CCU
0x08870000, `drivers/clk/sunxi-ng/ccu-sun60iw2-cpupll.c` B +0x2000/+0x201c
and `ccu_nkmp.c` recalc N/p divider. RTC selection follows existing read-only
board sampler. No vendor driver is copied as an RTOS lifecycle.

## Host and build validation

```sh
# On this minimal Ubuntu host ONLY, if bundled QEMU libraries are missing:
python3 tools/setup-a733-qemu-v208.py --download
# Downloads/extracts official packages into out/qemu-deps-v208, NEVER installs.
python3 tools/test-a733-perf-v208.py --output out/perf-check-v208-new
A733_V208_BUILD_DIR="$PWD/../../quickly-openvela/cmake_out/cubie-a7z_nsh_v205_camera_perf" \
  bash tools/build-a733-camera-v208.sh out/camera-build-v208-new out/ort-static-v185-r2 --incremental
```

20 groups include old 17, real ARM opcode + feature/affinity/selftest execution
in freestanding QEMU, actual old/new decoder byte comparison (random, ties,
NaN, infinity), and actual concurrent producer/coordinator/device-mock tests
with CRC/IRQ faults, capture failures, slow display, pthread-create failure,
stop during capture and stop after display. Host mocks are NOT board acceptance.
QEMU harness uses strict alignment, GPR-only and inline atomics for MMU-off
test RAM; these harness-only flags are NOT product flags. Native ARM objects,
full link, frame pool/stack/layout, optional ISA disassembly, 296-path SDK
byte-exact restoration and final image readback are separate gates.

## Board test (sequential, no concurrent NPU/audio/diagnostic workload)

Back up SD /data before flashing the new independent image. UFS is untouched.
If old UFS U-Boot takes control, use the new image's script only:

```text
sunxi_card0_probe
mmc dev 0
mmc rescan
mmc part
ext4load mmc 0:3 0x4fc00000 /boot/openvela/a733/camera-v208.scr && source 0x4fc00000
```

In NSH:

```text
display raw8
camera live-verify 5 1
```

Wait for completion, then `camera live-status`. Must have both
`equal-legacy=1`, all six CRC/detections equal, verified=1 and error/cleanup=0.
Expect `live208 cpu` to distinguish ARM CRC qualification from software fallback.
Do NOT interpret the verification run's extra two executions as displayed FPS.

After that run finishes, test unchanged-serial scheduling:

```text
camera live-stream 30 1
```

Wait for complete, save `camera live-status` and `cat /dev/a733-camera-control`.
Only then test the new two-slot path:

```text
camera live-pipe 30 1
```

Wait for complete, save the same two statuses and full log. Require frames ==
inference when every=1, monotonically changing input-bound frames, all six
outputs, no fallback/error/cleanup, proper clearing when a target leaves.
The `live208 pipe` line records produced/consumed/discarded/occupancy and
producer/consumer wait. Stage sums can exceed wall time because they overlap.
Consumer take/copy duration and producer CPU wall are NOT exposure/DMA-only
timing. Pending-at-stop frames are not counted as displayed or inferred.

Finally try `camera live-pipe 60 1`, then from another shell
`camera live-stop` during capture/processing; wait active=0 and cleanup=0.
On failure preserve status/control/dmesg and reboot, do not keep retrying.
Only after short passes try a longer 300-second run and inspect temperature.
30 FPS is still unmeasured; do not use every>1 or preview FPS as YOLO FPS.
The current sensor mode is nominally ~30 FPS; no new high-FPS mode is claimed.

## Recovery

Firmware source commit, exact image hashes, kernel CRC, actual link addresses,
final test evidence and restore checkpoint will be appended after full
verification. Until then this document records SOURCE-READY work only.
Rollback uses the complete sealed v207 image and its matching boot script,
never an old script with a new kernel.

### Build audit correction

The initial v208-r1 ELF audit caught a CMake directory-scope mistake: pixel
O2 applied but runtime O2 did not. The initial image is an INTERMEDIATE,
not the delivered candidate. The correction attaches the absolute source
property to the parent arch target. Final delivery uses a separately sealed
v208-r2 image and rechecks real compiler commands, ELF and whole-image hashes.

## Final v208-r2 delivery

SOURCE/BUILD/HOST/IMAGE gates PASSED; BOOT/HARDWARE/FPS/SOAK pending board.
20 groups, real ARM CRC execution in QEMU, native ARM objects/stack, full
link/ISA/local O2, 24 constructors, layout, 296 SDK paths restored byte-exact,
image readback and independent Windows full SHA all passed.

| Item | Final value |
| --- | --- |
| Image in repository parent | openvela-a733-cubie-a7z-sd-v208-r2-camera-crc-rgb-pipeline-candidate.img |
| Image bytes | 2147483648 |
| Image SHA256 | c7bf742a705d2866f6b31214744da3f70496336dd7857d967d30f7fc44c41207 |
| Kernel bytes / U-Boot CRC | 7152232 / 61e1f07c |
| Kernel SHA256 | 8e17544f36edb369b7ba8af130310225b92d4c3c142cf0c9097f06ff9f3fdc76 |
| Firmware source | 97f1426ed20824d743123a25af0c078e989e08d8 |
| Final source tag | a733-v208-r2-camera-perf-source-20261029 |
| Final seal tag | a733-v208-r2-camera-perf-sealed-20261029 |
| Binary/config/mode/script seal | out/v208-r2-release-20261029 (10 files) |
| Full source/Git checkpoint | out/checkpoints/a733-v208-r2-camera-perf-recovery-20261029 |
| Build/check/package | out/camera-build-v208-r2 / out/camera-check-v208-r2 / out/camera-package-v208-r2 |
| Permanent RAW frame pool | 0x45911000 / 4194304 bytes |
| Runtime end | 0x45de4000, no overlap |

[100 sealed evidence payloads](../evidence/camera-v208-final/README.md)
bind source, compiler flags, ISA, SDK restore, tests and final image.
Application RGB slots are heap allocations, not two RAW DMA buffers.
No physical disk/UFS writes or new board performance claims.

The unsubmitted r1 image remains an intermediate audit artifact:
`openvela-a733-cubie-a7z-sd-v208-camera-crc-rgb-pipeline-candidate.img`,
SHA 2c231bb8cfa5575059ad517dba5afb49c8a99a6c5f7cdc7ef75b0e1e9081379e.
Do NOT flash that instead of r2. Old v207 image SHA
b174d29ca6525397b7b7890afe9462e9fc30a1e3fcf73ce431a2fd4efdc65492
and checkpoint remain unchanged.

Recovery audit: the first restored Git index exposed an older LF-normalised
v207 serial-log entry versus its raw CRLF archive. The original bytes are
unchanged (SHA a1f84d8a680667e49a5da735d67e622ac11ab1be8046b5c5618832025ed1ba55).
Final recovery tag/checkpoint `a733-v208-r2-camera-perf-recovery-20261029`
records those raw bytes in Git too. The old seal/checkpoint is retained,
and firmware/image hashes do not change.

Recheck/repackage with NEW paths only:

```sh
python3 tools/check-a733-perf-v208.py --build ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v205_camera_perf --build-report out/camera-build-v208-r2 --output out/camera-check-v208-new --firmware-commit 97f1426ed20824d743123a25af0c078e989e08d8
python3 tools/audit-a733-isa-v208.py ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v205_camera_perf out/camera-check-v208-new/isa-audit.json
python3 tools/package-a733-camera-v208.py out/v208-r2-release-20261029 ../openvela-a733-v208-rebuilt.img out/camera-package-v208-new --firmware-commit 97f1426ed20824d743123a25af0c078e989e08d8
```
