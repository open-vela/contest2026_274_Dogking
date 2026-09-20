# A733 本地 LLM 适配：阶段 2

本阶段解决两项进入真实推理前必须通过的基础门槛：准确定位 SD 卡模型路径错误，以及验证 openvela/NuttX AArch64 环境能够完整编译、链接并运行 C++17 与 STL。它仍不是 llama.cpp 推理完成的声明。

## 已完成

- `aipetllm pathcheck [path]`：逐级检查路径分量，输出目录、普通文件、大小和准确的 `errno`。此前板上 `stat` 返回 `20`，即 `ENOTDIR`，表示 `/data/models/...` 的某个中间分量不是目录。
- `aipetllm readcheck [path]`：使用 1 MiB 缓冲区顺序读取完整模型，核对读入字节数，并计算 CRC32。该命令用于在接入推理引擎前排除 FTP、FAT 或大文件损坏。
- `aipetllm cxxcheck`：用 C++17 的 `std::vector` 和 `std::string` 执行确定性测试，验证构造、动态分配、迭代及 libc++/libc++abi 链接。
- 板级配置启用 LLVM libc++、任务/线程 TLS 和 C++17。没有使用工具链自带的 GNU libstdc++，因为其头文件与当前 NuttX libc 类型声明冲突。
- 主机侧完整构建和最终 C++ 链接已通过；`System.map` 中存在 `aipetllm_cxx_checkpoint`。

## 固化配置

```text
CONFIG_FS_LARGEFILE=y
CONFIG_HAVE_CXX=y
CONFIG_CXX_STANDARD="gnu++17"
CONFIG_LIBCXX=y
CONFIG_TLS_NELEM=4
CONFIG_TLS_TASK_NELEM=4
CONFIG_SYSTEM_AIPETLLM=y
```

`CONFIG_LIBCXX` 会通过 Kconfig 选择 `CONFIG_HAVE_CXXINITIALIZE` 和 `CONFIG_PTHREAD_MUTEX_TYPES`；低层 C++ ABI 的默认选择为 `CONFIG_LIBCXXABI`。

## 板上验证顺序

烧写本阶段镜像后先执行：

```text
aipetllm cxxcheck
aipetllm pathcheck /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf
aipetllm readcheck /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf
aipetllm gguf /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf
```

`cxxcheck` 成功时退出码为 0，并打印 C++ 标准、容器元素数和固定校验值 `027b1520`。

如果 `pathcheck` 显示 `/data/models` 是 `regular-file`，不要直接删除它。先用 FTP/SSH 把这个文件改名备份，再建立真正的 `/data/models/` 目录并重新上传模型。模型文件路径中应使用普通下划线 `_`，不要包含 Markdown 转义符 `\_`。

`readcheck` 需要顺序读取约 1 GiB 模型，耗时取决于 SD 卡；CRC32 只用于传输完整性检查，不替代模型发布方提供的 SHA-256。

## 构建证据

验证构建目录：

```text
cmake_out/cubie-a7z_nsh_v58_aipet_llm/
```

2026-09-11 构建产物：

```text
nuttx.bin   1594288 bytes
nuttx       2197968 bytes
System.map   225083 bytes
```

关键符号：

```text
path_check
aipetllm_main
aipetllm_cxx_checkpoint
```

## 下一阶段

下一阶段引入固定版本、MIT 许可的 llama.cpp 最小 CPU 后端，先以 ARM64/NEON、单线程、512 token 上下文完成以下闭环：

1. 打开并解析 Qwen2.5 1.5B `Q4_K_M` GGUF。
2. 创建模型与上下文，完成 tokenizer/chat template。
3. 执行一次短提示词的 prompt evaluation。
4. 贪心生成少量 token，并允许 `Ctrl+C` 中止。
5. 记录加载峰值内存、首 token 延迟和后续 token/s。

只有板上真实输出 token 后，才能称为本地 LLM 推理可用。VIP2 NPU 暂不作为 GGUF 执行后端；后续只能基于算子覆盖率评估矩阵乘等可拆分子图。
