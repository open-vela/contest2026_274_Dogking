# Evidence and handoff

Deliver:

- supported board/SoC revision and architecture;
- boot chain and exact boot command;
- memory/partition map and reserved regions;
- toolchain/dependency versions;
- commits, configuration, patches, and overlay manifest;
- one-command clean/incremental builds;
- image names, sizes, SHA-256 hashes, and flashing steps;
- recovery image and procedure;
- boot, fault, and subsystem logs;
- per-feature state: source-ready, build-passed, image-produced, boot-passed, hardware-passed, stress-passed;
- wiring/voltage notes;
- limitations, timeouts, security implications, and untested cases;
- dependency-ordered next tasks.

For every feature include a minimal positive test and a negative/recovery test. Keep secrets out; document provisioning with placeholders.
