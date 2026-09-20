# NPU runtime bring-up

## 0. Identity and lifecycle

Enable power, reset, and clocks in documented order; read immutable identity/revision; return to known idle. A register read alone does not prove execution.

## 1. Memory pool and DMA

Allocate bounded contiguous memory where required. Record CPU virtual, physical, NPU virtual addresses, size, alignment, page ownership, and address width. Add guards. Define cache clean/invalidate and barriers.

## 2. Accelerator MMU

Build tables to the hardware format. Validate page size, permissions, address truncation, alignment, invalid entries, boundary pages, and unmapped behavior. Never copy constants across revisions blindly.

## 3. Submission and completion

Submit the smallest legal command. Support IRQ completion and bounded polling fallback. Capture raw IRQ/idle registers. Implement timeout, cancel, reset, and post-reset reinitialization.

## 4. Stable ABI

Expose device/context creation, buffer allocation/import, map/unmap, cache sync, submit, wait, cancel, query, reset, and cleanup. Validate handles, sizes, overflow, ranges, ownership, and teardown with in-flight work.

## 5. Self-test

Use a deterministic operation that changes guarded output. Verify expected bytes/CRC, guards, repetition, timeout recovery, and release.

## 6. Tiny model

Run a vendor-validated small network with exact input/output hashes. Confirm model identity, metadata, relocations, cache operations, completion, and repeatability.

## 7. Production model

Use two or more non-equivalent inputs. Validate every output and postprocessing. Exercise repeat runs, unload/reload, memory pressure, and recovery.

Start with one synchronous context/core. Add queues, contexts, or cores only after correctness and reset recovery are stable.
