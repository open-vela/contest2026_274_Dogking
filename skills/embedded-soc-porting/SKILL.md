---
name: embedded-soc-porting
description: "Port and bring up a new embedded SoC or board for openvela, NuttX, or a comparable RTOS, from boot, CPU architecture, memory and interrupts through SMP, storage, buses, networking, peripherals, reproducible builds, and hardware validation. Use for new-chip, new-board, BSP, architecture, boot, SMP, driver, or hardware-enablement work; do not use for ordinary application-only changes on an already supported board."
---

# Embedded SoC Porting

Build the port as a sequence of independently provable checkpoints. Preserve a bootable baseline and never call source presence, compilation, or image creation “hardware success.”

## Start with facts

1. Locate repository instructions (`AGENTS.md`, contribution rules, vendor documentation, board schematics, TRM, boot logs, linker scripts, manifests, and existing sibling ports).
2. Identify the CPU architecture and privilege/boot ABI before changing code. Read [architecture-routing.md](references/architecture-routing.md) for the matching architecture.
3. Record the boot chain, image load address, entry state, memory map, UART, interrupt controller, timer, reset/clock controller, pinmux, storage, and available recovery path.
4. Separate architecture/kernel, board/vendor, openvela framework/service, and application responsibilities.
5. Create a recovery point: commit or patch bundle, known-good image hash, build command, boot command, and serial log.

When working in an official checkout, keep durable work in the project repository. Apply project-owned overlays or patches temporarily, build, and restore the official tree even on failure. Read [openvela-nuttx-boundaries.md](references/openvela-nuttx-boundaries.md).

## Execute gated bring-up

Follow [bringup-phases.md](references/bringup-phases.md). Do not advance past a failed foundational gate merely because a later subsystem appears to respond.

At every gate:

- add bounded timeouts to polling loops;
- emit compact register checkpoints and numeric error codes;
- retain the previous working image;
- test cold boot as well as warm reset;
- verify the exact artifact that will be flashed;
- distinguish `source-ready`, `build-passed`, `image-produced`, `boot-passed`, `hardware-passed`, and `stress-passed`.

Use [hardware-debug-playbook.md](references/hardware-debug-playbook.md) when a board hangs, faults, times out, or behaves nondeterministically.

## Preserve recoverability

- Never overwrite or delete an unknown block device. Resolve and display the exact target first.
- Do not replace the only bootable image. Version images and record SHA-256 hashes.
- Preserve user changes in dirty worktrees. Avoid destructive Git operations.
- Keep experimental changes isolated and reversible.
- Save the exact configuration, toolchain identity, commit IDs, patches, build log, boot log, and test transcript.
- Treat vendor Linux drivers as behavioral references, not drop-in RTOS drivers.

## Finish with evidence

Produce [evidence-and-handoff.md](references/evidence-and-handoff.md). State unsupported or untested features plainly. If a peripheral is absent, report a non-blocking probe result rather than claiming it works.

For a worked example of lessons from a heterogeneous AArch64 board, consult [a733-lessons.md](references/a733-lessons.md) only when relevant; never copy its addresses or register values to another chip.
