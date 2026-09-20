# Staged bring-up phases

## 0. Provenance and recovery

Capture manuals, schematics, firmware, toolchain, sources, partition map, known-good image, flashing/recovery procedure, and serial settings. Establish a source-controlled overlay before modifying upstream.

## 1. Primary CPU and early console

Reach C runtime on one CPU with controlled interrupts/caches. Prove `.bss`, stack, linker symbols, UART, and early fault output.

## 2. Exceptions, interrupts, and timer

Install vectors, acknowledge/end interrupts correctly, and verify a timer counter plus periodic interrupt. All register waits must time out and identify the failing register.

## 3. Memory, MMU/MPU, cache, and allocator

Validate RAM banks/reserved ranges, page attributes, permissions, device mappings, cache maintenance, barriers, large allocations, fragmentation, and address-width truncation.

## 4. Scheduler and SMP

Start secondary CPUs one at a time with distinct stacks/per-CPU state. Verify identity, IPIs, affinity, migration, locks, atomics, timers, and repeated boot. Stabilize before optimizing placement.

## 5. Storage and persistent configuration

Bring up at a safe clock, enumerate media, parse partitions, mount read/write storage, and test large files plus power-loss-tolerant configuration updates.

## 6. Buses and pin control

Validate GPIO, pinmux, clocks, resets, DMA, I2C, SPI, UART, PWM, and conflict ownership. A pin table becomes evidence only after register readback and a physical test.

## 7. Networking and services

Prove link/association, IP, ARP, DNS, time, TCP/UDP, TLS, and reconnect separately. Start services only after address and time are valid. Test from another host without manual ping warm-up.

## 8. Higher-level peripherals

Add display, audio, camera, GPU, NPU, Bluetooth, and sensors independently. Lazy-initialize optional hardware where a synchronous probe could block boot. Absence must be non-fatal unless required.

## 9. Product integration and soak

Combine gradually. Run cold-boot loops, sustained I/O, reconnect, thermal load, memory pressure, suspend/resume if supported, and watchdog recovery.

Each phase needs a command, expected/error output, timeout, evidence log, and rollback point.
