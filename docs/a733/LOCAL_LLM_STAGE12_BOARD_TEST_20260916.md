# A733 本地 LLM 阶段 12 真机记录（2026-09-16）

## 结论

Stage 12 已在 Cubie A7Z 真机通过。两个 token 的 K/V cache、causal GQA、
scaled dot-product、softmax 和 1536 维 attention context 均完成。

## 真机基线

    kv-cache tokens=2 kv-heads=2 head-dim=128 k-crc=bd0b5df4/98c3354b v-crc=64c8a0b6/818d0a00
    attention query-position=1 causal-keys=2 group=6 scale=0.0883883461 score-crc=d569d836 probability-crc=9a8a5baa
    head[0] scores=347.4773,347.3105 probs=0.5415983,0.4584017
    head[1] scores=1383.294,1391.932 probs=0.0001772203,0.9998228
    head[2] scores=57.03885,55.18724 probs=0.8643163,0.1356837
    context-crc=2b2120e4 sum=47.3412029 l2=14.6100536 min=-0.970682681 max=3.39355946

## 判定

- 两个 K/V cache CRC 与阶段 11 的 position 0/1 基线一致；
- 12 个 query heads 按 6:1 正确映射到 2 个 KV heads；
- 每个展示 head 的两个 softmax 概率之和为 1；
- attention context 有正常分布且 CRC 稳定；
- 可进入 attention output 投影、残差连接和 FFN 前 RMSNorm。
