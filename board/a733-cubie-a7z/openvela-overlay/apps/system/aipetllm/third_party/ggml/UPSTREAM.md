# ggml 上游来源

本目录包含从 llama.cpp 的 ggml CPU 量化内核中机械复制的最小文件集合，没有修改上游文件。

- 项目：`ggml-org/llama.cpp`
- 标签：`v0.1.2`
- 提交：`1511ce3bc3f087376c8526b4ad07100bfabb277f`
- 下载归档 SHA-256：`33ca5a5c5f67cf92dc754a18c46600db406dd7b1d546dcd27b4112d04b8bf9f9`
- 许可：MIT，见本目录 `LICENSE`
- 用途：先验证 AArch64/NEON 下 Q4_K/Q8_K 量化点积，再扩展到 Qwen2.5 模型加载和推理。

只复制了公共头文件、量化实现、通用 CPU 量化函数和 ARM 量化函数。GPU、RPC、动态后端、服务器、示例与模型转换工具均未纳入固件。
