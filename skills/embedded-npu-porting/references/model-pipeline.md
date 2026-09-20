# Model pipeline

## Classify artifacts

- Training/interchange graph: PyTorch, ONNX, TensorFlow, TFLite, Paddle, etc.
- Quantized interchange: graph plus scales, zero points, calibration metadata/data.
- Vendor compiled network: hardware/runtime-specific graph or blob.
- Prepared board package: compiled graph plus relocated commands, weights, descriptors, or traces for one CID/runtime/memory contract.

Do not rename one category as another. Preserve hashes and provenance.

## Conversion record

Record framework/opset, inputs/shapes/dtypes/layouts, dynamic axes, preprocessing, calibration set, quantization, unsupported/fused operators, compiler/container version, target CID, flags, warnings, and output hash.

## Runtime contract

For each tensor record name, rank, dimensions, layout, element type, quantization, bytes, alignment, and ownership. Confirm NCHW/NHWC, RGB/BGR, letterbox, mean/scale, signedness, and padding.

## Prepared packages

Include a versioned manifest: hardware ID, driver/runtime version, model hash, memory needs, relocations, I/O, command streams, and integrity checks. Reject mismatches.

## Proprietary formats

Use official tooling and terms. A captured execution is a diagnostic bridge, not a general compiler/loader. Do not distribute proprietary firmware, libraries, models, or reverse-engineered details without authorization.
