# A733 本地 LLM 阶段 1：板上测试记录（2026-09-11）

## 已通过

```text
nsh> aipetllm info
backend=CPU/ARM64 model=/data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf
stage=large-file/memory/GGUF validation; llama.cpp pending
recommended context=512 initial threads=2 quant=Q4_K_M

nsh> aipetllm probe 512
memory-probe requested=512 MiB allocated-touched=512 MiB chunks=32/32

nsh> aipetllm probe 1024
memory-probe requested=1024 MiB allocated-touched=1024 MiB chunks=64/64
```

结论：A733 4 GiB 配置下，阶段 1 的 512 MiB 和 1024 MiB 堆压力测试均通过。

## 待处理错误

```text
nsh> aipetllm gguf /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf
aipetllm: stat /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf failed: 20
```

错误码 20 对应 `ENOTDIR`，表示路径中某一级不是目录。当前记录只说明模型文件尚未被程序成功 `stat`，不能据此判断 GGUF 文件损坏。额度恢复后先检查：

```text
ls /data
ls /data/models
ls -l /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf
```

如果 `/data/models` 不存在，先执行 `mkdir /data/models`，再通过 FTP/SSH 上传模型。上传完成后重新运行 `aipetllm gguf`。

## 当前阶段边界

内存与命令入口已验证；GGUF 元数据解析和 llama.cpp 推理尚未开始。不要把本记录解释为 Qwen 已经能够生成文本。

