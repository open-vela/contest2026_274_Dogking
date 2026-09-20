# Hardware debug playbook

## Boot hang

Insert monotonic markers before/after each operation. Replace infinite polling with deadlines. Check power domain, clock, reset, pinmux, bus access, and device presence in that order. Keep optional peripheral initialization off the boot-critical path.

## Exception or fault

Capture CPU/hart ID, privilege level, cause/ESR, PC/ELR, fault address, stack pointers, status, link register, and general registers. Resolve the PC against the exact unstripped ELF and map. A zero return PC often indicates a bad function pointer, corrupted return address, or uninitialized secondary-core context.

## Missing or storming interrupt

Verify source enable/pending, routing/affinity, priority mask, trigger type, registration, acknowledge, source clear, and end-of-interrupt. Confirm the interrupt number from target documentation.

## DMA or data corruption

Check virtual/physical/device addresses, address width, alignment, ownership, cache clean before device reads, invalidate after device writes, barriers, descriptor lifetime, and completion. Use guards and deterministic patterns.

## Works under Linux only

Capture device tree, clocks, regulators, reset sequence, pinctrl, descriptors, transactions, firmware, and formats. Reimplement the contract with RTOS APIs; do not copy Linux lifecycle or locking assumptions unchanged.

## Intermittent network behavior

Separate RF/scan/association, authentication, DHCP, neighbor discovery, DNS, time, TCP, TLS, and application errors. Test bidirectional ARP, reconnect, stale sockets, partial I/O, and repeated requests.

## Garbled SMP console

Serialize output or use per-CPU trace buffers. Avoid verbose logging from every CPU during early rendezvous.

## Performance regression

Measure boot, I/O, compute, and end-to-end time separately. Record affinity, frequency, temperature, throttling, cache state, storage speed, and cold/warm state.
