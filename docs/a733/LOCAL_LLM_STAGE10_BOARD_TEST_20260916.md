# A733 本地 LLM 阶段 10 真机记录（2026-09-16）

## 结论

Stage 10 已在 Cubie A7Z 真机通过。`aipetllm qproj` 从约 1.1 GB 的
Qwen2.5-1.5B-Instruct Q4_K_M GGUF 中按需读取真实权重，完成 token embedding、
第 0 层 RMSNorm 和 `blk.0.attn_q.weight` 的 Q4_K×Q8_K 投影。

## token 9707

```text
embedding-crc=79ae820e normalized-crc=2ff9a1f6
qproj tensor=blk.0.attn_q.weight type=Q4_K input=1536 output=1536 row-bytes=864
q-crc=c2259e72 sum=-34.7462135 l2=51.2093496 min=-11.489624 max=9.86270714
q[0..7]=-0.2044705,-0.450916,-0.05513314,-0.3720448,-0.06430604,0.2475885,0.0573886,-0.04008815
```

## token 1879

```text
embedding-crc=face75fe normalized-crc=5f43e2dd
qproj tensor=blk.0.attn_q.weight type=Q4_K input=1536 output=1536 row-bytes=864
q-crc=1dbf0d3c sum=34.5563358 l2=35.7831006 min=-4.06766844 max=3.91083026
q[0..7]=0.145906,-0.01165111,-0.1998885,0.09339657,0.327347,-0.09337588,-0.1923675,-0.08664872
```

两次执行均以以下信息结束：

```text
Qwen2 layer-0 Q projection checkpoint passed; K/V and RoPE pending.
```

## 判定

- 两个 token 的 embedding/RMSNorm CRC 与阶段 9 基线一致；
- Q 投影输出维度为 1536，权重类型、输入维度和逐行字节数合理；
- 输出包含正常的正负浮点分布，CRC 随 token 改变；
- 阶段 10 通过，下一阶段可安全实现 K/V 投影与 RoPE。
