# A733 本地大模型阶段 13 真机验证

测试平台：Cubie A7Z / A733，openvela 最小 SD 镜像 v79。

## 测试命令

    aipetllm attnblock2 /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707 1879

## 关键结果

- 注意力输出投影：`blk.0.attn_output.weight`，Q4_K，1536 x 1536；
- attention output CRC：`f1bce89f`；
- 第一次残差 CRC：`84367675`；
- FFN 前 RMSNorm CRC：`06b03d70`；
- post-attention RMS：`0.335894147`；
- inverse RMS：`2.97711515`；
- 归一化向量统计：sum `55.0684886`，L2 `23.2887066`，min `-2.50361609`，max `3.13444328`。

真机输出成功标志：

    Qwen2 layer-0 attention output/residual/FFN-norm checkpoint passed; SwiGLU feed-forward network pending.

结论：第 0 层注意力输出投影、第一次残差连接以及 FFN 前归一化均已在真机通过，
可以进入 `gate/up -> SiLU/SwiGLU -> down -> 第二次残差` 阶段。
