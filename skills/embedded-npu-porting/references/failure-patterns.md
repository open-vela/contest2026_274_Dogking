# NPU failure patterns

## False success

- Completion changes but output does not.
- Captured command returns the same output for every input.
- Only zero input was tested.
- Linux runtime succeeds but RTOS never executes hardware.
- Host conversion succeeds but target CID/runtime rejects the model.

## Memory/coherency

- CPU virtual submitted as physical/NPU virtual.
- 64-bit address truncated.
- Commands/tables not cleaned before submit.
- Outputs not invalidated after completion.
- Buffers freed while owned by hardware.
- Tensor padding/alignment omitted.

## Commands/relocation

- Prepared streams retain reference physical addresses.
- Relocations omit descriptors or secondary tables.
- Runtime/firmware changes command ABI.
- Captured masks/core IDs mismatch target.

## Semantics

- NCHW/NHWC or RGB/BGR mismatch.
- Wrong signedness, scale, or zero point.
- Bad letterbox mapping/dequantization.
- Heads swapped or wrong decoder family.

## Lifecycle

- Timeout poisons later tasks.
- Reset omits MMU/cache/runtime reinit.
- Errors leak contexts/mappings/pages.
- Concurrent use exceeds proven queue support.

Diagnose identity, power/clock/reset, addressing, coherency, command integrity, completion, raw output, tensor semantics, then postprocessing.
