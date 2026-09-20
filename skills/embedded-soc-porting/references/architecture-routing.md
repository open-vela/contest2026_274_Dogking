# Architecture routing

Choose the architecture branch before editing startup, interrupt, MMU, or SMP code. Confirm the actual core from architectural ID registers, not product marketing.

## AArch64 / ARMv8-A and later

Record the entry exception level, `SPSR`, `SCR`, `HCR`, load address, DTB location, and whether firmware uses PSCI, spin tables, or a vendor mailbox. Validate the image format expected by the bootloader; a Linux-style ARM64 Image header normally requires `booti`, not a raw `go` jump.

Bring up exception vectors and per-CPU stacks, GIC distributor/redistributors, architectural timer, MMU (`MAIR`, `TCR`, translation tables), caches and barriers, then secondary CPUs. On heterogeneous systems, read `MPIDR_EL1` and `MIDR_EL1` on each CPU and maintain a logical-to-hardware map.

## ARMv7-A / ARMv7-R

Confirm ARM/Thumb entry state and CPU mode. Establish vector placement, mode stacks, CP15 setup, MMU/MPU, cache maintenance, interrupt controller, timer, and SMP coherency where applicable. Do not reuse AArch64 exception or page-table code.

## Cortex-M / ARMv7-M / ARMv8-M

Validate vector table address, initial MSP, `VTOR`, clock tree, SysTick or hardware timer, NVIC priorities, FPU context, MPU, and security attribution. Do not introduce MMU or GIC assumptions.

## RISC-V

Record M/S/U-mode entry, firmware/SBI contract, hart IDs, device tree, `satp` mode, PMP, timer, and interrupt controller (CLINT/ACLINT, PLIC, IMSIC/AIA). Establish trap vectors, per-hart stacks, timer, and IPI before SMP. Use appropriate instruction/data fences and cache maintenance.

## x86 / x86-64

Record firmware and boot protocol, CPU mode transition, GDT/IDT, paging, ACPI tables, local and I/O APIC, timer source, and AP startup. Validate identity mappings and higher-half transitions before secondary CPUs.

## Other or mixed architectures

Derive the equivalent reset vector, privilege level, exception table, interrupt controller, timer, address protection, coherency, secondary-core launch, and ABI. Treat auxiliary DSP, MCU, or safety cores as separate targets with explicit mailbox, shared-memory, and cache contracts.

## Exit criteria

- deterministic early console;
- faults print PC, cause, fault address, stack, and registers;
- timer interrupts survive stress;
- heap/stack regions match the linker map;
- cache/MMU tests pass with DMA disabled and enabled;
- every configured CPU reports its expected ID and runs a pinned task;
- repeated cold boots produce the same topology and memory result.
