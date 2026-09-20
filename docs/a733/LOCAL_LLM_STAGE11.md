# A733 本地大模型阶段 11：带 bias 的 Q/K/V 与 RoPE

本阶段把第 0 层注意力输入推进到完整 Q/K/V 投影和位置编码：

- 读取 `blk.0.attn_q/k/v.weight`；
- 加入 Qwen2 的 `blk.0.attn_q/k/v.bias`；
- 从 GGUF 读取 query heads、KV heads、RoPE 维度和频率基数；
- 验证 Qwen2 GQA 的 query/KV head dimension 一致；
- 对 Q、K 执行 GGUF/ggml normal RoPE，V 保持不变；
- 输出 bias 前后的稳定 CRC，供真机回归。

Stage 10 的 `qproj` 保留为不带 bias 的矩阵乘法基线。新的完整路径为：

```text
aipetllm qkv /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707 0
aipetllm qkv /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 1879 1
```

成功标志：

```text
Qwen2 layer-0 biased Q/K/V and RoPE checkpoint passed; attention scores and KV cache pending.
```

下一阶段将使用两枚 token 的 K/V 建立最小 KV cache，并计算 causal grouped-query
attention、softmax 和注意力输出。

## 构建与镜像

- openvela 边界检查：通过；
- 全量隔离构建：通过；
- 内核大小：`1668408` 字节；
- 内核 SHA-256：`cba626de4c04e85aa5948ca5423188714f603cf3002ca360ae55d1ea5bab174c`；
- 可烧写镜像：`openvela-a733-cubie-a7z-sd-aipet-qkv-rope-v77-candidate.img`；
- 镜像大小：`2147483648` 字节；
- 镜像 SHA-256：`588167cabce8d19e6565920bdccc37539b2d80f1f648c515f8a8cdedccb77047`。

镜像已通过 ext4 只读检查、内嵌内核哈希检查和 GPT 完整性检查。
