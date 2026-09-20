# A733 本地 LLM 阶段 7：Qwen2 token 解码

## 目标

在不把完整 15 万词表常驻内存的前提下，从实际 GGUF 流式读取指定 token piece，执行
GPT-2/Qwen2 byte-level Unicode 逆映射，并在开发板上组合出原始 UTF-8 字节序列。

这一步验证生成阶段最终输出的 token ID 能够被可靠还原为文本，也是完整 BPE 编码器
和逐 token 推理之前的独立验收点。

## 实现约束

- 应用继续位于 openvela apps/system/aipetllm，只使用公开 libc/文件系统接口。
- 不访问 A733 私有诊断节点，不依赖 Linux syscall 或 Linux 用户态库。
- 每次最多解码 64 个 token；GGUF 词表流式扫描，按实际 token 数动态分配缓冲区，
  最大约 64 KiB。
- token piece 上限为 511 字节；当前实测模型最大值为 256 字节。

## 板端命令

NSH 不使用反斜杠续行，命令必须整行输入：

    aipetllm decode /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707 1879

预期末尾包含：

    decoded-bytes=11 text='Hello world'
    Qwen2 token-to-piece byte decode checkpoint passed; encoding pending.

如果实际模型的 token ID 与 llama.cpp 测试词表不一致，命令仍会安全输出对应 piece；
此时需以该模型配套 tokenizer 的 golden token ID 重新验收，不能据此判为驱动故障。

## 边界

本阶段完成 token ID 到文本的 byte-level 解码，不代表 prompt 到 token ID 的 BPE 编码
已经完成，也不代表 Transformer graph 已经执行。下一阶段实现固定中英文 prompt 的
Qwen2 预分词与 BPE merge，并与参考运行时逐 token 对比。

## 构建与镜像

- ARM64 openvela 内核大小：1635584 字节
- 内核 SHA256：431467204164b772c5d8c9cdbd193a1bb92e7668eb3e12bb7d810ddb9e65f1e7
- 可烧写镜像：openvela-a733-cubie-a7z-sd-aipet-token-decode-v73-candidate.img
- 镜像大小：2147483648 字节
- 镜像 SHA256：706e3d3d9ded25128cd0eb1626a8c4b4cbd1868819c72ee86785d00b7d3a8baf

镜像已通过 ext4 只读文件系统检查、嵌入内核逐字节/哈希核对和 GPT 检查。GPT 工具
仅保留历史镜像已有的“分区 4 未按 2048 sector 边界结束”提示，没有发现分区错误。

v73 已在 Cubie A7Z 上使用真实 1.5B 模型完成解码验收，结果见
docs/a733/LOCAL_LLM_STAGE7_BOARD_TEST_20260915.md。
