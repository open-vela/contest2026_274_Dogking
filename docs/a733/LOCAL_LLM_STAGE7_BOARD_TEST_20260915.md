# A733 本地 LLM 阶段 7 真机记录（2026-09-15）

## 结论

Cubie A7Z 使用 v73 镜像和真实 Qwen2.5-1.5B-Instruct Q4_K_M 模型，通过
token ID 到 UTF-8 文本的 byte-level 解码验收。

## 执行命令

    aipetllm decode /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707 1879

## 真机输出

    token[9707] piece-bytes=5 decoded-bytes=5
    token[1879] piece-bytes=7 decoded-bytes=6
    decoded-bytes=11 text='Hello world'
    Qwen2 token-to-piece byte decode checkpoint passed; encoding pending.

## 判定

- token 9707 正确解码为 5 字节的 Hello。
- token 1879 的 GGUF piece 为 7 字节，经过 GPT-2 byte-level Unicode 逆映射后
  正确解码为 6 字节的空格加 world。
- 拼接结果为 11 字节的 Hello world，与参考结果逐字节一致。
- 测试直接读取开发板上的 1117320736 字节实际模型，不是裁剪词表或宿主机替代结果。

阶段 7 完成。下一验收点是固定英文和中文 prompt 的 Qwen2 预分词及 BPE 编码，
并将板端 token ID 与模型配套参考 tokenizer 逐项比较。
