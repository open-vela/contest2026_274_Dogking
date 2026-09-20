# A733 本地 LLM 阶段 8：Qwen2 prompt 编码

## 目标

在 openvela 的 aipetllm 应用内直接读取 GGUF tokenizer 资产，把英文和中文 prompt
编码为 Qwen2 token ID。实现不启动 Linux 进程，不依赖 Python，也不访问 A733 私有
诊断节点。

## 已实现

- 从 GGUF v2/v3 加载 tokenizer.ggml.tokens 与 tokenizer.ggml.merges。
- 建立 token ID 和 BPE merge rank 索引。
- 实现 GPT-2/Qwen2 的 256 字节 Unicode 映射。
- 实现 Qwen2 常用预分词分支：缩写、字母、CJK、单数字、空白、换行、标点及 emoji。
- 按最低 merge rank 和最左优先规则执行 BPE。
- 新增 aipetllm encode 命令。

## 宿主 golden 结果

英文：

    prompt: Hello world
    expected: 9707,1879
    actual:   9707,1879

中英混合：

    prompt: Hello, y'all! How are you [emoji] ?[CJK]apple[CJK]1314151[CJK]
    expected: 9707,11,379,64848,0,2585,525,498,26525,223,937,104100,18493,22377,99257,16,18,16,19,16,20,16,35727,21216
    actual:   9707,11,379,64848,0,2585,525,498,26525,223,937,104100,18493,22377,99257,16,18,16,19,16,20,16,35727,21216

24 个 token 与 llama.cpp 随附 Qwen2 golden 向量逐项一致。

## 板端验收

NSH 必须整行输入：

    aipetllm encode /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf "Hello world"

预期：

    tokens=2 ids=9707,1879
    Qwen2 pre-tokenizer and byte-level BPE checkpoint passed

随后回译：

    aipetllm decode /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707 1879

## 当前边界

核心英文/CJK 路径已经通过 golden，但完整 Unicode category 表尚未导入。罕见文字、
组合音标和复杂尾随空白仍需全套 tokenizer corpus 审计。因此本阶段可用于 AI 桌宠的
中英文固定 prompt 验证，暂不宣称与参考 tokenizer 对所有 Unicode 输入完全等价。

模型 Transformer graph、KV cache 和采样仍未执行。

v74 已在 Cubie A7Z 上使用真实 1.5B 模型完成 encode/decode 闭环，记录见
docs/a733/LOCAL_LLM_STAGE8_BOARD_TEST_20260915.md。

## 构建与镜像

- ARM64 openvela 内核大小：1656120 字节
- 内核 SHA256：5d6d0b6b59d8005917a7615d52f888251bd62f204191a6f56478f2ebdebd1327
- 可烧写镜像：openvela-a733-cubie-a7z-sd-aipet-bpe-v74-candidate.img
- 镜像大小：2147483648 字节
- 镜像 SHA256：e0e220ea310c6bfcddfd5527e7733788294083d7dab6ca01d0b9eab49a3aba06

镜像已通过 ext4 检查、嵌入内核哈希核对和 GPT 检查。分区 4 的 2048-sector
对齐提示继承自已验证的基础镜像，不是本阶段引入的文件系统或分区错误。
