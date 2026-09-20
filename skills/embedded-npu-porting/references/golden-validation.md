# Golden validation

## Golden run

On the supported reference system record hardware/revision, runtime versions, model hash/metadata, exact input/preprocessing, every raw output hash, decoded result, load/prepare/inference/postprocess/wall time, memory, frequency, and temperature.

## Validation ladder

1. Sizes/layouts match.
2. Command completes with guards intact.
3. Exact bytes/CRC match for deterministic integer execution.
4. Otherwise compare tensor tolerances and task accuracy.
5. Distinct inputs produce appropriate distinct outputs.
6. Warm repeats detect stale cache/replay.
7. Unload/reload and cold boot detect uninitialized state.
8. Timeout/malformed input verifies recovery.

Use zero, one, gradient, and real fixtures. Log input hashes beside output hashes. Stable output across different inputs is a failure unless semantics justify it.

Report load, preparation, first inference, warm median/tail, preprocessing/postprocessing, end-to-end throughput, thermal throttling, CPU use, and fallback percentage separately.
