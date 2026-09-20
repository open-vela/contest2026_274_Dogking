# A733 本地大模型阶段 13：注意力输出、残差与 FFN 前归一化

本阶段在两 token causal attention context 后继续执行第 0 层注意力子层：

- 读取并执行 blk.0.attn_output.weight；
- 把位置 1 的原始 residual stream 加回输出投影；
- 读取 blk.0.ffn_norm.weight；
- 使用模型 epsilon 执行 FFN 前 RMSNorm；
- 输出投影、残差与 post-attention normalized vector 的 CRC。

## 真机命令

    aipetllm attnblock2 /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707 1879

成功标志：

    Qwen2 layer-0 attention output/residual/FFN-norm checkpoint passed; SwiGLU feed-forward network pending.

下一阶段将执行第 0 层 ffn_gate、ffn_up、SiLU/SwiGLU、ffn_down 和第二次残差，
从而完成一个完整 Transformer block。

## 构建与镜像

- openvela 边界检查：通过；
- 全量隔离构建：通过；
- 内核大小：1672504 字节；
- 内核 SHA-256：54016f1bd0414385cbea037cb1260f8b783931e331a0f30664afb8f38a45b203；
- 可烧写镜像：openvela-a733-cubie-a7z-sd-aipet-attnblock-v79-candidate.img；
- 镜像大小：2147483648 字节；
- 镜像 SHA-256：cb29e10e525dea37c2a6cec7263e6c0a07be85e5df9c3149ffdd66b904f3203f。

镜像已通过 ext4、内嵌内核哈希和 GPT 完整性检查。
