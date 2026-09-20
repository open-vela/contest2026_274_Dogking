# A733 本地 LLM 适配：阶段 1

本阶段把 AI 桌宠的本地 LLM 入口加入 openvela A733/Cubie A7Z 镜像，目标是先在真实板上验证大模型文件、4 GiB 内存和 GGUF 格式，再接入推理后端。当前阶段不会把模型塞进固件：模型应放在 SD 卡 `/data/models/`，这样可以替换模型而不重新烧写系统。

## 已完成

- 新增 `apps/system/aipetllm/`，编译命令为 `aipetllm`。
- 打开 `CONFIG_FS_LARGEFILE`，支持访问大于 2 GiB 的模型文件偏移（具体上限仍受 FAT 文件系统和可用空间影响）。
- `aipetllm info` 输出目标后端、默认模型和当前阶段。
- `aipetllm probe [MiB]` 分块申请并触碰内存页，验证模型加载所需的堆压力；默认 1024 MiB，允许 1..3072 MiB。
- `aipetllm gguf [path]` 校验 GGUF v2/v3 头部并读取架构、量化类型、上下文长度、层数、嵌入维度、注意力头数和 tokenizer 词表数量。
- 默认目标是 Qwen2.5-1.5B-Instruct `Q4_K_M`，但阶段 1 不执行张量加载或推理。

## 板上验证

把 Qwen GGUF 文件通过 FTP/SSH 放到 `/data/models/` 后执行：

```text
mkdir /data/models
aipetllm info
aipetllm probe 1024
aipetllm gguf /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf
```

先用 `probe 512`，确认基本内存余量，再逐步升到 `1024`。不要使用 `probe 3072` 作为日常命令，它只是边界压力测试，会暂时占满大部分用户堆。

预期最后一行是：

```text
GGUF metadata checkpoint passed; tensor loading/inference pending.
```

这表示文件和元数据有效，不表示已经能生成文本。

## 构建产物

本次验证构建目录：

```text
cmake_out/cubie-a7z_nsh_v58_aipet_llm/
```

主要产物：

```text
nuttx.bin
nuttx
System.map
```

配置中必须存在：

```text
CONFIG_FS_LARGEFILE=y
CONFIG_SYSTEM_AIPETLLM=y
CONFIG_AIPETLLM_PROGNAME="aipetllm"
```

## 下一阶段边界

Qwen GGUF 不能直接提交给 A733 VIP2 NPU。VIP2 当前链路接受已经转换/准备好的专有模型包；Transformer 的 attention、KV cache、RoPE 和采样流程还没有对应的 NPU 图编译与运行时。因此下一阶段应先把 llama.cpp 的 ARM64/NEON CPU 后端以最小子集移植进 openvela，优先完成：

1. 单轮 prompt 的 tokenizer、embedding、RMSNorm、MatMul、RoPE 和 attention。
2. `Q4_K_M` 权重读取、KV cache 和贪心采样。
3. 512 token 上下文、单线程可重复输出。
4. 再启用 A733 多核调度；当前板级配置仍是 `CONFIG_SMP_NCPUS=1`。
5. 最后评估把独立的 MatMul/elementwise 子图转换给 VIP2，不能假设整张 Qwen 图可直接跑 NPU。

1.5B Q4_K_M 模型通常约 1 GiB，模型、KV cache、工作区和系统必须同时留在 4 GiB 内存与当前 `/data` 分区中；真正推理前必须用板上 `free` 和上下文压力测试确认余量。

