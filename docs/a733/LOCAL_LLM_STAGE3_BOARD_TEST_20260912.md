# A733 本地 LLM Stage 3 板端验证（2026-09-12）

## 结论

Cubie A7Z 实机上的 C++17/libc++ 和 GGML Q4_K×Q8_K ARM64 NEON 内核均验证通过。本阶段镜像可以作为继续移植 GGML CPU backend 和 Qwen GGUF 推理链路的稳定基线。

## 实测结果

```text
nsh> aipetllm cxxcheck
cxx-check backend='ARM64 C++17/STL' standard=201703 vector=6 checksum=027b1520

nsh> aipetllm quantcheck
ggml-quant-check q4_k/q8_k elements=256 neon=1 reference=-1.183411 result=-1.18341 error=9.536743e-07
```

确认项：

- C++ 标准值为 `201703`，即 C++17。
- `std::vector`/`std::string` 检查完成，校验值稳定。
- `neon=1`，说明使用了 AArch64 ASIMD/NEON 路径。
- 参考结果与优化结果误差为 `9.536743e-07`，明显小于 0.001 验收阈值。

## 模型路径检查结果

```text
OK path=/data type=directory size=0
FAIL path=/data/models errno=2
```

`errno=2` 是 `ENOENT`，表示 `/data/models` 不存在，因此 `readcheck` 和 `gguf` 当前无法继续。日志中顶层 `stat` 显示的 `errno=20` 不代表 GGUF 损坏；逐级路径检查已经定位到真正原因是缺少目录/文件。

当前 512 MiB 镜像的 `/data` 分区约为 64 MiB，不能容纳约 1 GiB 的 Qwen2.5-1.5B Q4_K_M 模型。下一步需要先制作更大的数据分区镜像或使用另一块大容量存储，再把模型放到：

```text
/data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf
```

上传后依次执行：

```text
aipetllm pathcheck /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf
aipetllm readcheck /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf
aipetllm gguf /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf
```

只有这三项通过后，才进入 GGUF 张量映射和完整 token 生成验证。
