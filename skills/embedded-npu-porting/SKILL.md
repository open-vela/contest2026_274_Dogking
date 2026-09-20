---
name: embedded-npu-porting
description: "Port and validate an embedded NPU, VIP, or AI accelerator stack, including power/clock/reset, identity, MMU and DMA, cache coherency, IRQ and task ABI, vendor model conversion, prepared artifacts, golden-output comparison, dynamic-input proof, performance, and end-to-end inference. Use for accelerator bring-up or deploying CNN, YOLO, ASR, TTS, or LLM workloads to an NPU; do not use for CPU-only ML application work."
---

# Embedded NPU Porting

Treat NPU enablement as three separate deliverables: hardware runtime, model toolchain, and product inference pipeline. Success in one does not prove the others.

## Establish constraints first

1. Identify accelerator generation, CID/revision, cores, address width, firmware/runtime, vendor compiler, operators, quantization, and redistribution restrictions.
2. Run the vendor Linux/reference runtime unchanged to establish a golden baseline.
3. Distinguish source graphs, vendor-compiled networks, and board-prepared packages.
4. Record input layout, color order, normalization, dimensions, quantization, output semantics, and postprocessing.
5. Create a recovery commit/image before changing MMU, DMA, cache, IRQ, or global memory layout.

## Bring up the runtime in gates

Use [runtime-bringup.md](references/runtime-bringup.md). Required order:

1. power, clock, reset, identity;
2. contiguous DMA pool and cache ownership;
3. accelerator MMU/address translation;
4. IRQ/poll completion, timeout, cancel, reset;
5. context, buffer, task, sync, cleanup ABI;
6. deterministic self-test;
7. tiny golden model;
8. real model with changing input;
9. end-to-end product pipeline.

Never allow an unbounded hardware wait. A task needs a deadline and a recovery path that leaves the device reusable.

## Handle models explicitly

Read [model-pipeline.md](references/model-pipeline.md) before converting/importing a model. If the compiled format is proprietary, do not claim a generic loader from one captured execution. Use vendor-supported tooling or a legally permitted, versioned prepared package.

Validate with [golden-validation.md](references/golden-validation.md). Matching an all-zero input is insufficient; prove multiple known inputs change outputs correctly.

## Integrate without overclaiming

- Keep the low-level ABI narrow and integer-based where possible.
- Separate load/prepare latency from warm inference latency.
- Report CPU fallback and hybrid execution honestly.
- Prefer CNN/detection first. Move autoregressive LLM layers only after confirming operators, dynamic shapes, KV cache, memory, and tool support.
- Preserve model and vendor-runtime license constraints.

Read [workload-routing.md](references/workload-routing.md) for YOLO, ASR, TTS, and LLM decisions, and [failure-patterns.md](references/failure-patterns.md) for suspicious success.

## Finish with evidence

Deliver model hashes, converter version/options, package version, input fixtures, output hashes/tolerances, logs, cold/warm timing, memory, temperature, reset recovery, and separate status for hardware, runtime, model, preprocessing, inference, postprocessing, and application.

Use [vip2-lessons.md](references/vip2-lessons.md) only as an example; never reuse its addresses or binaries on another accelerator.
