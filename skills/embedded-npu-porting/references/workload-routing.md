# Workload routing

## CNN and detection

YOLO/image CNNs are usually first targets. Validate resize/letterbox, color order, quantization, all output heads, anchor/anchor-free decode, confidence, NMS, and coordinate mapping. Measure camera-to-result latency.

## Face recognition

Separate detection, alignment, embedding, and matching. Record normalization/distance metric. Treat templates as sensitive biometric data.

## ASR

Split capture, resampling, features, encoder, decoder, endpointing, and normalization. Fixed encoder blocks may suit NPU while streaming/stateful decoding stays on CPU. Measure real-time factor and dropped audio.

## TTS

Separate text normalization, acoustic model, vocoder, buffering, and playback. Confirm sequence/operator support and do not block audio on accelerator reset.

## Autoregressive LLM

TOPS alone is insufficient. Confirm attention, RoPE, RMSNorm, SwiGLU, quantized matrices, dynamic sequence, KV-cache read/write, sampling, and memory. Otherwise use CPU/GPU/hybrid while retaining an NPU backend boundary.

Report first-token latency, warm tokens/s, context length, KV memory, and thermal stability separately from model load.

## Product routing

Expose capability queries and explicit fallback. State backend, fallback reason, latency, and whether the accelerator actually ran the graph.
