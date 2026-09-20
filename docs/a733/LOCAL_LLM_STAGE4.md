# A733 本地 LLM 阶段 4：独立大模型存储分区

## 目标与状态

本阶段解决 Qwen 1.5B 等大模型无法放入原 512 MiB SD 镜像的问题，并保持已有 Wi-Fi 固件、SSH 配置、NPU 数据和用户文件不变。

当前状态：代码已在 A733/Cubie A7Z 配置下完成全量构建；生成的 v61 镜像将原 512 MiB 镜像扩展为 3 GiB，并新增约 2.5 GiB 的 FAT32 模型分区。镜像不内置 Qwen 模型，避免分发第三方模型和无谓增大仓库体积。

最终镜像：`A733-A7Z-ALL-Files/local-llm-stage4-model-storage-a733/openvela-a733-cubie-a7z-sd-local-llm-v61-models-3g.img`。

镜像 SHA256：`3c4012d2b383effeb5ef6682273af56c2edd90b167662fbbe6a65c0719814a92`。

内核 SHA256：`06c814be465cfe3a1dc79b668b0552573f5e33927bcddfa91f3c41a4c26360c8`。独立提取镜像第 3 分区中的 `Image` 后复核，与构建产物完全一致。

## 分区策略

- GPT 1～4：继承已验证的最小 SD 镜像。
- 第 4 分区 `/dev/a7z-data`：继续挂载到 `/data`，保留 Wi-Fi 固件、SSH/FTP 配置及 NPU 文件。
- 新增第 5 分区 `/dev/a7z-models`：从 LBA `1048576` 开始，挂载到 `/data/models`。
- 老版 512 MiB 镜像没有第 5 分区时，系统仍创建 `/data/models` 目录并回退到原 `/data` 分区，保持向后兼容。

板端可通过以下命令检查：

```text
mount
ls /dev
ls /data/models
aipetllm pathcheck /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf
```

## 模型传输

FTP 服务根目录已配置为 `/data/models`，因此可以直接上传 GGUF 模型。也可以使用已经适配的 SSH/SCP。密码、Wi-Fi 密码及模型文件都不应提交到 Git。

上传后先运行：

```text
aipetllm gguf /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf
aipetllm readcheck /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf
```

这两个命令只验证路径、GGUF 头及大文件顺序读取，并不代表完整 Qwen 推理已经实现。

## 构建与封装

v61 的全量构建目录为：

```text
quickly-openvela/cmake_out/cubie-a7z_nsh_v61_llm_models
```

无须 sudo 的镜像封装命令：

```bash
bash contest2026_274_Dogking/tools/package-a733-local-llm-large-image.sh \
  A733-A7Z-ALL-Files/local-llm-stage3-ggml-a733/openvela-a733-cubie-a7z-sd-local-llm-v59-ggml.img \
  A733-A7Z-ALL-Files/openvela-a733-cubie-a7z-sd-local-llm-v61-models-3g.img \
  quickly-openvela/cmake_out/cubie-a7z_nsh_v61_llm_models/nuttx.bin \
  3
```

脚本仅复制已验证的基础镜像、替换第 3 分区中的 openvela 内核、修复 GPT 备份表并创建第 5 个 FAT32 模型分区；不会修改输入镜像。

## 已完成与未完成边界

已完成：ARM64 C++17/libc++、GGML Q4_K/Q8_K 与 NEON 数学检查、GGUF/大文件检查命令、独立模型分区和可烧写镜像封装。

尚未完成：GGML CPU backend 图执行、Qwen 张量加载、tokenizer、KV cache、采样器和逐 token 文本生成。因此本阶段是完整本地 LLM 的可靠存储与运行时前置阶段，不应对外宣称已经能够运行 Qwen 对话。
