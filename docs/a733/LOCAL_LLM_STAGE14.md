# A733 本地大模型阶段 14：完整 Transformer Block

本阶段在阶段 13 的注意力残差与 FFN 前归一化基础上完成 Qwen2 第 0 层：

- 执行 `blk.0.ffn_gate.weight`；
- 执行 `blk.0.ffn_up.weight`；
- 计算稳定形式的 SiLU 与 SwiGLU；
- 执行 `blk.0.ffn_down.weight`；
- 将 down projection 加回注意力残差流，完成第二次残差连接；
- 输出 gate、up、SwiGLU、down 与完整 block 输出 CRC。

## 真机命令

    aipetllm block2 /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707 1879

成功标志：

    Qwen2 layer-0 full Transformer block checkpoint passed; multi-layer execution pending.

本阶段完成后，单层 Qwen2 Transformer 的所有主要计算算子均已闭环。下一步将把同一执行器
参数化并推广到 28 层，同时引入可变长度 KV cache、最终 RMSNorm、LM head 与采样循环。

## 构建与镜像

- openvela 公共 API 边界检查：通过；
- 全量隔离构建：通过；
- 官方工作区自动恢复检查：通过；
- 内核大小：1676600 字节；
- 内核 SHA-256：`74efaa420cfaaa210326036f50e5a78132c5f7d5380df11985ccc4a4ada089c1`；
- 可烧写镜像：`openvela-a733-cubie-a7z-sd-aipet-block-v80-candidate.img`；
- 镜像 SHA-256：`9ae8f21eecdc33226d4824fd0a1756ffbaf240c8d6a32e1d50c4ccdd23c23a8c`。

镜像已通过 ext4 文件系统检查、内嵌内核字节数与 SHA-256 一致性检查以及 GPT 校验。
