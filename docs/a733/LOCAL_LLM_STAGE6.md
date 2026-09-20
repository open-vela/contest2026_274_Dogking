# A733 本地 LLM 阶段 6：Qwen2 tokenizer 资产验证

## 阶段目标

在引入完整 tokenizer 和 GGML graph 前，先确认板上实际 Qwen2 GGUF 的 BPE 资产可以
安全读取。该检查直接运行在 openvela `aipetllm` 应用内，不依赖 Linux、Python 或
本机 HTTP 推理服务。

## 新命令

```sh
aipetllm tokencheck /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf
```

命令以流式方式扫描 GGUF metadata，不把十几万个 token 和 merge 一次性常驻内存，
并验证：

- `tokenizer.ggml.model` 为 `gpt2`；
- `tokenizer.ggml.pre` 为 `qwen2`；
- token 词表和 BPE merge 表存在且每个字符串长度受限；
- token type、score 数组存在时，其元素数与词表一致；
- EOS 必须存在，BOS/EOT/PAD 存在时不得越过词表；
- 所有计数、总字节数和最大条目长度可以输出用于后续内存预算。

成功标志：

```text
Qwen2 BPE tokenizer asset checkpoint passed; tokenization is pending.
```

## 边界说明

该阶段使用 openvela 应用构建、文件系统和标准 C 接口。Qwen2 tokenizer 是官方
openvela 当前未提供的模型运行时能力，因此在 `aipetllm` 内移植；它不会读取 A733
私有设备节点，也不会把桌宠业务绑定到 NuttX 私有头文件。

## 后续阶段

通过真机验证后，引入与该 GGUF 完全匹配的 Qwen2 regex pre-tokenizer、byte-level BPE
和 special token 处理，并为固定中文提示词建立 token ID 黄金结果。只有 prompt 能在
板端稳定编码和解码后，才接 Qwen2 首层 GGML graph 与 KV cache。

## 构建与镜像恢复点

- ARM64 openvela 全量构建：通过；
- `nuttx.bin` SHA-256：`cf1fe67e3132cf7cde8eb00ad3819a9e6a4fbe1252ccb3edd9c574529593486c`；
- 可烧写镜像：`openvela-a733-cubie-a7z-sd-aipet-tokencheck-v72-candidate.img`；
- 镜像 SHA-256：`d8f6397340dc071dda59bcfcb728600707d440d5459a8e8a7b117d47783ce7c6`；
- 镜像继承 v71 已验证的 Wi-Fi、NPU、存储和网络服务，只替换 openvela 内核。

宿主功能测试使用 llama.cpp 随附的 `ggml-vocab-qwen2.gguf`，成功识别 151936 个
token、151387 条 merge、151936 个 token type，且特殊 token 索引检查通过。宿主
样例通过只证明解析器逻辑正确，最终完成仍以开发板上的实际 1.5B 模型输出为准。

实际 1.5B 模型的 v72 真机验证已经通过，完整输出见
`docs/a733/LOCAL_LLM_STAGE6_BOARD_TEST_20260915.md`。
