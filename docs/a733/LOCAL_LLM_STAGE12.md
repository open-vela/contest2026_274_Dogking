# A733 本地大模型阶段 12：两 token causal GQA 与 KV cache

本阶段使用 Hello world 的两个真实 token 建立最小 KV cache，并执行第 0 层
causal grouped-query attention：

- token 9707 位于 position 0，token 1879 位于 position 1；
- 两个 token 都执行 embedding、RMSNorm、带 bias Q/K/V 和 RoPE；
- KV cache 保存两个位置的 2 个 KV heads；
- 12 个 query heads 按 6:1 映射到 KV heads；
- 对 position 1 计算两个 causal key 的缩放点积；
- 对每个 head 执行数值稳定的双元素 softmax；
- 使用概率加权 V，生成 1536 维注意力上下文。

## 真机命令

    aipetllm attn2 /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707 1879

成功标志：

    Qwen2 two-token causal GQA/KV-cache checkpoint passed; attention output projection pending.

下一阶段将读取 blk.0.attn_output.weight，执行注意力输出投影、残差连接和
post-attention RMSNorm。

## 构建与镜像

- openvela 边界检查：通过；
- 全量隔离构建：通过；
- 内核大小：1668408 字节；
- 内核 SHA-256：38511f6999577f9696d9cd063b21d991fce226621d129122396ca295115d45b0；
- 可烧写镜像：openvela-a733-cubie-a7z-sd-aipet-attn2-v78-candidate.img；
- 镜像大小：2147483648 字节；
- 镜像 SHA-256：8d26fa6c5a27f2ab84288a2d1ee39e5fb7e936eb432cfcf8ed066d8287850e83。

镜像已通过 ext4、内嵌内核哈希和 GPT 完整性检查。
