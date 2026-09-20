# A733 本地 LLM 阶段 6 真机记录（2026-09-15）

## 结论

v72 在 Cubie A7Z 上通过实际 Qwen2.5-1.5B-Instruct Q4_K_M 模型的 tokenizer 资产和
张量目录双重验证。阶段 6 完成，可以进入 Qwen2 byte-level BPE 编码/解码。

## 模型文件

```text
/data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf
size=1117320736 bytes
GGUF version=3
```

## Tokenizer 验证

执行：

```sh
aipetllm tokencheck /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf
```

真机结果：

```text
tensors=339 metadata=26
tokenizer model='gpt2' pre='qwen2'
vocab=151936 merges=151387 token-types=151936 scores=0
token-bytes=1372758 max-token=256
merge-bytes=1520452 max-merge=257
bos=151643 eos=151645 eot=UINT64_MAX pad=151643 add-bos=0 add-eos=-1
Qwen2 BPE tokenizer asset checkpoint passed
```

`eot=UINT64_MAX` 和 `add-eos=-1` 表示对应可选 GGUF key 不存在，不是越界或读取
错误。EOS、BOS 和 PAD 均在 151936 词表范围内。

## 张量目录验证

执行：

```sh
aipetllm modelcheck /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf
```

真机结果：

```text
architecture=qwen2 tensors=339 metadata=26
alignment=32 data-offset=5950496
tensor-payload=1059 MiB
F32=141 tensors
Q4_K=169 tensors, 721 MiB
Q6_K=29 tensors, 337 MiB
layers=28 roots=token+norm+output layer-mask=01ff
qwen2-check passed
```

## 尚未完成

本记录证明模型容器、量化布局和 tokenizer 资产正确，不代表已经执行语言模型或生成
文本。下一验收点必须给定固定中英文 prompt，输出可重复的 token ID 和可逆解码结果；
随后才进入首层 graph、KV cache、采样和逐 token 生成。
