# A733 本地大模型阶段 15：28 层统一执行器与 LM Head

阶段 14 已在真机完成单层 Transformer Block 数值闭环。本阶段不再逐算子扩展，而是建立
参数化的全模型执行主干：

- 从 GGUF tensor directory 动态解析 `blk.0` 到 `blk.27`；
- 循环执行 attention RMSNorm、单 token GQA value path、attention output；
- 循环执行 FFN RMSNorm、gate/up、SiLU/SwiGLU、down 和两次残差；
- 执行最终 `output_norm.weight`；
- 执行 `output.weight` LM Head，计算完整词表 logits 和 argmax；
- 每层输出 state CRC，便于与 Linux/llama.cpp 黄金结果逐层定位。

单 token attention 中 softmax 只有一个 causal key，概率严格为 1，因此可跳过 Q/K dot-product，
直接按 GQA 映射展开 V；该优化与标准 attention 数学等价。多 token 生成阶段会恢复 Q/K、RoPE
和逐层 KV cache。

## 真机命令

    aipetllm forward1 /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707

成功标志：

    Qwen2 28-layer single-token forward and LM head checkpoint passed; multi-token KV-cache generation pending.

## 构建与镜像

- openvela 公共 API 边界检查：通过；
- 全量隔离构建：通过；
- 官方工作区恢复检查：通过；
- 内核大小：1680696 字节；
- 内核 SHA-256：`037177bc975c1b636e855811e9baf7c2c5d3559a6f3499ad6d21d4e7de893181`；
- 可烧写镜像：`openvela-a733-cubie-a7z-sd-aipet-forward1-v81-candidate.img`；
- 镜像大小：2147483648 字节；
- 镜像 SHA-256：`00ddafa52f0e039fa3be60a6f0363325d30bb89c11f7978d386cc6ecf4c6517c`。

镜像已通过 ext4、内嵌内核哈希和 GPT 完整性校验。
