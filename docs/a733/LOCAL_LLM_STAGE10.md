# A733 本地大模型阶段 10：Qwen2 第 0 层 Q 投影

本阶段在已经通过真机验证的 Qwen2 tokenizer、token embedding 和第 0 层
RMSNorm 基础上，执行第 0 层注意力的真实 Q（query）线性投影。

## 实现范围

- 从 GGUF 定位 `blk.0.attn_q.weight`，验证维度、类型和文件偏移；
- 保持模型在 `/data/models`，不把约 1.1 GB 的 GGUF 打入启动镜像；
- 对 RMSNorm 输出进行 Q8_K 临时量化；
- 使用 GGML 的 ARM64 `Q4_K/Q6_K × Q8_K` 向量点积逐行计算；
- F32 权重提供低内存回退路径；
- 只分配输入、输出、单行权重和 Q8_K 临时缓冲，不展开整张权重矩阵；
- 输出 Q 向量 CRC32、统计量和前 8 个元素，便于后续版本回归。

## 真机命令

```text
aipetllm qproj /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707
aipetllm qproj /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 1879
```

成功标志：

```text
Qwen2 layer-0 Q projection checkpoint passed; K/V and RoPE pending.
```

## 边界

本阶段完成的是第一个 Transformer 注意力层的 Q 投影，不代表完整 LLM
推理已经完成。下一阶段需实现 K/V 投影、RoPE、注意力打分和 KV cache。

## 构建与镜像

- 全量隔离构建：通过；
- openvela 边界检查：通过；
- 内核大小：`1664312` 字节；
- 内核 SHA-256：`42b61611f8482024011db4fbea16a3ea9b9bdc6e38bc615f856cccaec13ee242`；
- 可烧写镜像：`openvela-a733-cubie-a7z-sd-aipet-qproj-v76-candidate.img`；
- 镜像大小：`2147483648` 字节；
- 镜像 SHA-256：`aece14649d4812645929b6d2fb279c11056bce6268a579e6de9787781dd45c47`。

镜像已通过 ext4 只读检查、内嵌内核逐字节哈希检查和 GPT 完整性检查。
