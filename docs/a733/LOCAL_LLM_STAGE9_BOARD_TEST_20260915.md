# A733 本地 LLM 阶段 9 真机记录（2026-09-15）

## 结论

Cubie A7Z 使用 v75 镜像和真实 Qwen2.5-1.5B-Instruct Q4_K_M 模型，完成两个
token 的 embedding 按行读取、Q4_K 反量化和第 0 层 RMSNorm。Stage 9 通过。

## 模型布局

    hidden=1536
    vocab=151936
    token_embd.weight type=Q4_K
    row-bytes=864
    blk.0.attn_norm.weight epsilon=9.99999997e-07

1536 个元素正好包含 6 个 QK_K=256 的 Q4_K block；每行 6×144=864 字节，
与 GGML block_q4_K 布局一致。

## Token 9707

    input-rms=0.0242003009
    inverse-rms=41.2865677
    embedding-crc=79ae820e
    normalized-crc=2ff9a1f6
    sum=-15.929815
    l2=25.8291599
    min=-4.0575037
    max=3.02679038
    normalized[0..7]=-0.1866615,0.5314401,0.1519875,-0.1944195,0.5415596,0.1752138,-0.1888089,-0.3907051

## Token 1879

    input-rms=0.0249889922
    inverse-rms=39.9856148
    embedding-crc=face75fe
    normalized-crc=5f43e2dd
    sum=12.4697119
    l2=25.8792254
    min=-3.83666182
    max=4.23020601
    normalized[0..7]=0.1451908,-0.5831035,0.6280305,-1.396235,0.1115446,0.7240041,0.459402,-1.097582

## 判定依据

- 两个 token 的 embedding CRC 和 normalized CRC 均不同，排除固定缓冲区假结果。
- 输入 RMS、逆 RMS、L2、最小值和最大值均为有限数。
- epsilon 与 Qwen2 GGUF 元数据一致。
- 数值链路直接读取实际 1117320736 字节模型，仅按需读取目标行。
- Stage 8 的 9707、1879 分别对应 Hello 和空格加 world，因此已形成
  prompt→token→embedding→RMSNorm 的真实链路。

下一验收点是读取 blk.0.attn_q.weight，对归一化向量执行量化矩阵乘法，输出 Q
projection 的维度、CRC 和统计量；随后依次加入 K/V、RoPE 和 KV cache。
