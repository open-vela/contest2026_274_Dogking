# v209 sealed candidate, NOT board acceptance

102 byte-exact evidence payloads bind the firmware source, 22 host regression
groups, ARM object/stack checks, final ELF/link/ISA and local O2 checks,
297-path SDK restoration, image readback/GPT/e2fsck/non-root span comparison,
and independent Windows full-image SHA256. summary.json contains all hashes.
No physical disk/UFS write, new hardware/FPS/soak acceptance or private
camera photograph is claimed.

Image (repository parent):
openvela-a733-cubie-a7z-sd-v209-camera-affinity-async-candidate.img

- Bytes: 2147483648
- SHA256: 758208e54be76ff4fa985d94530dc50389790f88c9885200707b8332597057b4
- Kernel bytes: 7156328; CRC32: d67240da
- Kernel SHA256: 8a49d22720a97ad6ce0db38a36230e1c7715edec4d56bd10b8c507b37da8500d
- Firmware source: efe3f2efdc0fdac557098292ee4fc60810baa03e
- Source tag: a733-v209-affinity-async-source-20261029
- Seal tag: a733-v209-affinity-async-sealed-20261029
- Recovery tag/checkpoint: a733-v209-affinity-async-recovery-20261029
- Binary/config/mode/script seal: out/v209-release-20261029 (10 files)
- Build: out/camera-build-v209-r1
- Accepted complete check: out/camera-check-v209-r2
- Package: out/camera-package-v209-r1
- RAW pool: 0x45912000, 4194304 bytes; runtime end 0x45de5000
- Constructors: 24; largest checked function stack: 3152 bytes

The first check's additional ISA script assumed cpu_wait survived inlining.
Actual final ELF has usleep(1000) inside live_cpu_bind and the bracketed
MPIDR/MIDR reads inside cpu_identity. The revised audit verifies these actual
instructions. The full r2 rerun passed; the premature r1 summary is NOT
accepted. No firmware change, weakened migration condition or second image
was used to resolve that audit issue.

See [board procedure](../../a733/IMX214_V209_AFFINITY_ASYNC_TEST.md).
First run camera live-cpu; only real CPU6/7 proof and hardware-dispatch CRC
results qualify A76/CRC. Then cold/warm six-output equality, serial baseline
and opt-in async test. v208-r2 actual stable 7.733FPS is the previous result,
not a claim about v209. v204/v206/v207/v208-r2 and their recovery points remain.

Large images, sealed binaries, QEMU packages and ORT static libraries are
external hash references in the source/Git recovery checkpoint. Keep those
files or back them up separately; the source archive does not duplicate them.
