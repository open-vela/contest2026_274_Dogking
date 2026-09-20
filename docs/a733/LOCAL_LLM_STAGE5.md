# A733 本地 LLM 阶段 5：Qwen2 GGUF 张量目录验证

## 阶段结论

本阶段把 aipetllm 从“只读取少量 GGUF 元数据”推进到完整扫描模型张量目录。新增的 modelcheck 命令不会一次性把 1.5B 模型读入内存，而是以固定上限的目录结构验证后续推理所需的模型布局。

    aipetllm modelcheck /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf

命令会执行以下检查：

- GGUF v2/v3 文件头、元数据数量和张量数量；
- general.architecture=qwen2、general.alignment 和 qwen2.block_count；
- 每个张量的名称、维度、GGML 类型、块大小、文件偏移和实际字节数；
- F32/F16/BF16、Q4_0/Q4_1、Q5_0/Q5_1、Q8_0/Q8_1、Q2_K 至 Q8_K 的布局；
- 所有张量均按 GGUF 对齐要求存放且没有越过文件末尾；
- Qwen2 根张量 token_embd.weight、output_norm.weight；
- 每一层的注意力归一化、Q/K/V、注意力输出、FFN 归一化、gate/up/down 九组权重；
- 模型采用独立 output.weight 或合法的 tied output。

成功输出中的关键标志为：

    qwen2-check passed layers=<层数> roots=token+norm+<output|tied-output> layer-mask=01ff
    model directory is safe for staged tensor loading; inference is pending.

## 为什么先做这一层

完整 llama.cpp v0.1.2 已保存在资料目录，但它的宿主实现包含线程、正则表达式、mmap 和动态后端发现。直接整体复制到 NuttX 会同时引入过多未验证的 POSIX 假设。张量目录验证器先建立一个只依赖 openvela/NuttX 标准文件接口的安全边界，后续可以按层读取或映射权重，并把错误准确定位到模型文件、量化类型或具体层。

## 无开发板时的验证

本阶段可以完成交叉编译、链接和镜像封装。实机恢复后再执行：

    aipetllm info
    aipetllm cxxcheck
    aipetllm quantcheck
    aipetllm pathcheck /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf
    aipetllm modelcheck /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf

## 尚未完成

modelcheck 成功不等于已经生成文本。下一阶段仍需移植 GGML CPU graph/backend、实现分块权重装载、Qwen2 tokenizer、KV cache、采样器和逐 token 输出。只有开发板实际输出可读回复并通过重复运行稳定性测试后，才能声明本地 Qwen 对话完成。
