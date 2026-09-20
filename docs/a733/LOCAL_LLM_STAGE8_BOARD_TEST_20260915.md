# A733 本地 LLM 阶段 8 真机记录（2026-09-15）

## 结论

Cubie A7Z 使用 v74 镜像和真实 Qwen2.5-1.5B-Instruct Q4_K_M 模型完成
prompt 编码与 token 解码闭环，Stage 8 通过。

## 编码

执行：

    aipetllm encode /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf "Hello world"

真机结果：

    vocab=151936 merges=151387 prompt-bytes=11 spans=2
    tokens=2 ids=9707,1879
    Qwen2 pre-tokenizer and byte-level BPE checkpoint passed

## 解码

执行：

    aipetllm decode /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707 1879

真机结果：

    token[9707] piece-bytes=5 decoded-bytes=5
    token[1879] piece-bytes=7 decoded-bytes=6
    decoded-bytes=11 text='Hello world'

## 判定

- 真实模型词表和 151387 条 BPE merge 已成功建立索引。
- Hello world 被分成两个 Qwen2 span。
- 编码 token ID 与参考 golden 值 9707、1879 完全一致。
- 两个 ID 回译后逐字节恢复原始 11 字节文本。
- 测试没有调用 Linux、Python 或外部 tokenizer 服务。

阶段 8 完成。下一阶段开始读取 token_embd.weight、output_norm.weight 和第一层
Qwen2 张量，并建立量化张量按需加载及首个数值计算验收点。
