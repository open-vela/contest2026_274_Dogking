# A733 本地 LLM 阶段 11 真机记录（2026-09-16）

## 结论

Stage 11 已在 Cubie A7Z 真机通过。模型元数据确认 Qwen2.5-1.5B 使用
12 个 query heads、2 个 KV heads、128 维 head 和 1,000,000 RoPE base。

## token 9707，位置 0

```text
qkv heads=12/2 head-dim=128 position=0 rope-dim=128 base=1000000
biased-crc q=7d600aab k=bd0b5df4 v=64c8a0b6 dimensions=1536/256/256
rope-crc q=7d600aab k=bd0b5df4 v-unchanged=64c8a0b6
```

位置为 0 时旋转角度为 0，因此 Q/K 的 RoPE 前后 CRC 相同。

## token 1879，位置 1

```text
qkv heads=12/2 head-dim=128 position=1 rope-dim=128 base=1000000
biased-crc q=b15b4988 k=e28ce756 v=818d0a00 dimensions=1536/256/256
rope-crc q=bbdd93b0 k=98c3354b v-unchanged=818d0a00
```

位置为 1 时 Q/K CRC 随 RoPE 改变，V CRC 保持不变，符合注意力位置编码预期。

## 判定

- Stage 10 的无 bias Q 矩阵乘法 CRC 保持不变；
- Q/K/V bias 已加入，三路输出 CRC 均非零；
- GQA 的 query/KV head dimension 均为 128；
- RoPE 的位置 0 恒等与位置 1 旋转性质同时通过；
- 可进入两 token KV cache、causal grouped-query attention 和 softmax 阶段。
