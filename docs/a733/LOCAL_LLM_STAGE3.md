# A733 本地 LLM 阶段 3：GGML ARM64 量化内核

## 状态

本阶段已完成并通过 A733 交叉编译和系统链接。镜像包含 C++17/libc++ 运行时、`aipetllm` 命令以及 GGML v0.1.2 的 Q4_K/Q8_K ARM64 量化算子。

这不是完整的 llama.cpp 或 Qwen 推理器；它是接入完整模型前的真实数学内核检查点。

## 已集成内容

- `aipetllm cxxcheck`：验证 C++17/libc++、`std::vector` 和 `std::string`。
- `aipetllm quantcheck`：构造确定性输入，执行 Q4_K×Q8_K 量化、反量化和 ARM64 dot product，并与浮点参考值比较。
- `pathcheck`、`readcheck`、`gguf`：用于定位模型路径错误、验证大文件顺序读取和 GGUF 文件完整性。
- GGML 文件均来自官方 `ggml-org/llama.cpp` v0.1.2，提交 `1511ce3bc3f087376c8526b4ad07100bfabb277f`，归档 SHA256：`33ca5a5c5f67cf92dc754a18c46600db406dd7b1d546dcd27b4112d04b8bf9f9`。上游文件保持未修改并附带 MIT 许可证。

## 构建验证

构建目录：`quickly-openvela/cmake_out/cubie-a7z_nsh_v58_aipet_llm`。

产物大小：`nuttx.bin` 1,598,384 字节，`nuttx` 2,205,056 字节，`System.map` 225,677 字节。符号表已确认包含：

- `aipetllm_ggml_quant_checkpoint`
- `ggml_vec_dot_q4_K_q8_K`
- `quantize_row_q4_K`、`quantize_row_q8_K`
- `dequantize_row_q4_K`、`dequantize_row_q8_K`
- `aipetllm_cxx_checkpoint`

## 板端验证命令

启动本阶段镜像后执行：

```text
aipetllm cxxcheck
aipetllm quantcheck
```

预期 `quantcheck` 报告 `neon=1`（AArch64 ASIMD）且优化结果与参考结果误差不超过 0.001。若 `neon=0`，仍可验证功能，但说明编译配置未启用 ARM SIMD。

## 下一阶段

下一步应先接入 GGML CPU backend 初始化和小型矩阵/图执行，再实现 GGUF 元数据和张量映射，最后才接入 Qwen 1.5B 的 tokenizer、KV cache、采样和分块推理。不要直接把 1.5B 模型声明为已支持；4GB 内存需要从 Q4_K_M、短上下文（建议 512）和 1~2 个线程开始验证。
