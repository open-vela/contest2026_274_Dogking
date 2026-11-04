# v1.2／v220 预览封存证据

本目录是**软件和镜像证据，不是本版板上验收**。最新稳定版本仍是 [v1.1／v219](../product-v11-v219/board-accepted-20261101.json)。

- [总清单](summary.json)：实际固件 `133662be8c0f369f87ae1fc6ef24df2768960ed0`，预览镜像 2,147,483,648 字节；SHA256 `89a6d7f7d508427da0b4f193566c49d958451cc52c8e628a680f32a1062200a1`。
- [27 项门禁](host-arm-summary.json)：保留旧版全部 26 项；新增默认单轮、旧配置隔离、工作区分配失败及复用检查。实际 ARM 对象含 v1.2 路径，不只检查 Kconfig 文本。
- [实际配置／分配器回归](preview-profile-v220.log)：ASan／UBSan；引擎和 PCM 在此测试中是模拟的，不是性能分数。
- [真实 GGUF 数值报告](real-gguf-summary.json)、[完整日志](real-gguf-run.log)：实际 MiniCPM5，独立 llama.cpp 对照，6 组冷／热／batch logits 最大误差 0，2 个可选三轮记忆样例、4 个单轮自然 EOS 样例；主机计时不是 A7Z 延迟。原生模型的自我介绍仍需应用身份规则，未宣称原模型完全服从人设。
- [完整链接／摄像头／ABI／ISA](full-link-summary.json)、[ELF](elf-audit.json)：24 个构造函数、既有 NPU ABI 和硬件保护检查、实际预览标记；没有放宽未声明的源变更。允许的新增变更仅包括本次 CPU 预填充 `.inc`。
- [镜像与模型回读](package-summary.json)：真实 ASR/TTS、SD 备用 MiniCPM5、YOLO、SOUL、SWOUT1 启动脚本回读；稳定 v1.1 原镜像前后 SHA 不变。
- [Windows 独立 SHA256](windows-image-hash.json) 与 Linux 打包校验一致。
- [SDK 还原](sdk-restored.json)：暂存 316 项按字节还原。

`resident-lease-v219.log`、`runtime-lease-v219.log`、`wifi-v219.log` 是继承测试在本次源码上的重新执行，特意保留原测试名，不冒充新驱动开发或真实 AP 验收；`product-service.log` 也使用模拟引擎/PCM。

本版没有新实机听音、云 API、机械动作或声学首音测量，不宣称 3～5 秒已达标。不提交 PR、不推送，不擦写 UFS 或物理 SD。
Git 预览恢复标签为 `a733-v1.2-v220-preview-20261101`；私有全量恢复目录为 `out/checkpoints/a733-v12-v220-recovery-20261101`，仅在生成且校验 `COMPLETE` 后可用。
[构建、测试、上下文取舍与回退](../../PRODUCT_V1_2_V220_PREVIEW.md)。
