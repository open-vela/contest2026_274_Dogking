# A733 本地大模型阶段 14 真机验证

测试平台：Cubie A7Z / A733，openvela 最小 SD 镜像 v80。

## 测试命令

    aipetllm block2 /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707 1879

## 关键结果

- FFN intermediate size：8960；
- gate / up 类型：Q4_K / Q4_K；
- gate CRC：`82a4fe9e`；
- up CRC：`c60d9a47`；
- SwiGLU CRC：`d354efb5`；
- down projection CRC：`a657c3af`；
- 完整 block 输出 CRC：`28c5afaa`；
- block 输出统计：sum `27.4491091`，L2 `22.3654979`，min `-4.69229269`，max `3.45557451`。

真机最终输出：

    Qwen2 layer-0 full Transformer block checkpoint passed; multi-layer execution pending.

结论：Qwen2 第 0 层的 attention、两次 residual、RMSNorm、SwiGLU FFN 均已在
A733 真机闭环通过。下一阶段直接构建参数化的 28 层统一执行器、最终归一化和 LM Head。
