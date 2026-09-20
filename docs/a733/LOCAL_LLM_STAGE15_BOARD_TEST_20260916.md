# A733 本地大模型阶段 15 真机验证

测试平台：Cubie A7Z / A733，openvela 最小 SD 镜像 v81。

## 测试命令

    aipetllm forward1 /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707

输入 token 9707 对应此前 tokenizer/decode 检查中的 Hello。

## 逐层 state CRC

| 层 | CRC | 层 | CRC |
|---:|:---:|---:|:---:|
| 0 | f35f4323 | 14 | 6b594e33 |
| 1 | 43750cf3 | 15 | bd2b9356 |
| 2 | 18d69f16 | 16 | 9c91d253 |
| 3 | aa231488 | 17 | 8a8199d9 |
| 4 | a209163e | 18 | 564ad6b6 |
| 5 | c82850bb | 19 | 8794d9c7 |
| 6 | dfd26c49 | 20 | 66b6f7e4 |
| 7 | 435d8044 | 21 | 665a5f69 |
| 8 | d86e8a55 | 22 | 60f67a8e |
| 9 | 835bb607 | 23 | ffaad38b |
| 10 | 94662d84 | 24 | 70e99d11 |
| 11 | c4c62b55 | 25 | 5f3d61e4 |
| 12 | a18160cd | 26 | 486fd0fa |
| 13 | 9a2fe25c | 27 | 451d1506 |

## 最终输出

- 最终归一化 state CRC：17a04f7d；
- 完整 logits CRC：7fdfee67；
- 词表大小：151936；
- argmax token：6233；
- argmax logit：10.4909735。

使用同一 GGUF tokenizer 解码 argmax：

    aipetllm decode /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 6233

真机返回：

    decoded-bytes=10 text=' beautiful'

因此单 token 输入 `Hello` 已完成 `28 层前向 -> LM Head -> argmax -> tokenizer decode`
端到端闭环，当前贪心预测续写为 `Hello beautiful`。

真机最终输出：

    Qwen2 28-layer single-token forward and LM head checkpoint passed; multi-token KV-cache generation pending.

## 性能基线

使用 NSH time 对同一命令计时：

    time "aipetllm forward1 /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707"

真机耗时：

    473.6016 sec

即单次完整前向约 7 分 53.6 秒，等效吞吐约 0.00211148 token/s。测试期间：

- openvela 配置为 CONFIG_SMP_NCPUS=1；
- aipetllm 矩阵执行为单线程；
- 任务运行在启动 CPU0（A733 的 Cortex-A55）；
- GGUF 权重从 microSD/FAT 按矩阵流式读取；
- 尚未启用 A76、SMP、多核分块、跨 token 权重缓存和持久 KV cache。

此数值作为单 A55、单线程、SD 流式读取版本的冻结性能基线。后续所有 SMP/A76/NEON/
缓存优化必须同时保持本文件记录的逐层 CRC、最终 logits CRC 和 argmax 结果。

结论：流式 GGUF 读取、28 层参数化执行、最终 RMSNorm、完整 LM Head 和 argmax 已在
A733/openvela 真机通过。上述逐层 CRC 作为后续多 token 执行器的单 token 回归黄金基线。
