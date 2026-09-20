# A733 本地 LLM 阶段 9：真实 embedding 与 RMSNorm

## 目标

从 Qwen2.5-1.5B GGUF 中按 token ID 读取一行 token_embd.weight，按张量实际类型
反量化为 FP32，然后执行第 0 层注意力之前的 RMSNorm。这是本地 LLM 从格式解析进入
真实 Transformer 数值计算的第一个验收点。

## 实现

- 解析 general.alignment 和 qwen2.attention.layer_norm_rms_epsilon。
- 定位 token_embd.weight 与 blk.0.attn_norm.weight。
- 校验 hidden size、vocab size、token 范围、张量维度、类型和文件偏移。
- 仅按需读取一个 embedding row，不把整个 embedding 矩阵载入内存。
- 支持 F32、Q4_K、Q6_K embedding row。
- 使用已集成的 GGML 量化实现反量化。
- 执行 Qwen2 RMSNorm：y = x * rsqrt(mean(x*x) + epsilon) * weight。
- 输出输入/输出 CRC、RMS、L2、范围以及前 8 个结果，供真机重复性比较。

## 板端命令

NSH 中整行输入：

    aipetllm embed /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707

成功标志：

    Qwen2 token embedding and layer-0 RMSNorm checkpoint passed; Q projection pending.

该命令对 Hello 的首 token 9707 做实际模型计算。还可以用 1879 验证第二个 token。

## 边界

本阶段执行了真实 embedding 反量化和第 0 层 RMSNorm，但尚未计算 Q/K/V projection、
RoPE、attention、FFN、KV cache 或 logits，不能据此宣称已经完成文本生成。

## 构建与镜像

- ARM64 openvela 内核大小：1660216 字节
- 内核 SHA256：5a658f2e52009d9cf06315d2efa8ef9ffc6edceef55070a24e7f3d889d86f824
- 可烧写镜像：openvela-a733-cubie-a7z-sd-aipet-embedding-v75-candidate.img
- 镜像大小：2147483648 字节
- 镜像 SHA256：ec3abbb54d39012c205b6aefbac1d4aad66707476230d09c9a43dcd33e8b1759

镜像已通过 ext4、嵌入内核哈希和 GPT 独立检查。分区 4 对齐提示继承自基础镜像，
校验工具报告 No problems found。

v75 已使用真实 1.5B 模型在 Cubie A7Z 上通过两个 token 的数值验收，完整结果见
docs/a733/LOCAL_LLM_STAGE9_BOARD_TEST_20260915.md。
