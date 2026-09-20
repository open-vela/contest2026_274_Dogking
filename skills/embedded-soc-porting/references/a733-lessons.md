# A733 lessons: non-portable case study

These are examples, not register templates.

- The RTOS image carried an ARM64 Image header and required the bootloader's Image-aware path.
- Eight-core startup required correct per-CPU entry state, GIC redistributors, stacks, and fault diagnostics; optimistic SMP changes produced secondary faults at address zero.
- Runtime ID registers confirmed six Cortex-A55 and two Cortex-A76 CPUs.
- Multi-CPU early logging became interleaved; compact markers and later structured logs were more useful.
- Wi-Fi required separate firmware, scan, WPA2, DHCP, ARP, DNS, NTP, TCP, and service evidence.
- Synchronous optional display initialization blocked the shell; lazy user-triggered initialization preserved bootability.
- Linux was a valuable golden reference, but RTOS lifecycle, memory, and concurrency differed.
- Project-owned patches and wrappers kept the official checkout recoverable and submission-safe.
