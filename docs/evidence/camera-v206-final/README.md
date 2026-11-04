# v206 连续采集修复候选：构建与镜像封存

2026-10-29。**不是实机连续采集/FPS/长稳通过；需要用户上板验证。**

- 原始故障：[v205 用户日志](../camera-v205-board-failure-20261029/README.md)。
- 固件源码 `8cb44ea2dab68709203e3ae3f04ec2058bf0c603`，源标签 `a733-v206-camera-stream-source-20261029`。
- 包装工具提交 `5dd4d8a1232fefd1fc65395160f42795eac00158`；最终交付标签 `a733-v206-camera-stream-sealed-20261029`。
- 完整构建 `out/camera-build-v206-r1` 返回 0，293 个临时覆盖 SDK 路径逐字节恢复。
- 15 组主机回归、原生 ARM、完整链接与 ELF 通过。原 v204/v205 路径、像素/CRC/显示/模型常驻/同输入冷热核验保留。
- 新测试：归档 v205 在保守“地址就绪被消费”模型中重放第二帧 `00220083`，修复版 64 帧与 26 个回调后生效错误通过。
  **这只是软件假设回归，不是对芯片内部地址锁存机制的硬件证明。**
- 真实生产 broker/app 模拟：第二帧 EBADMSG 停止后 `error=-74 cleanup=0`；真正排空失败保持时钟、DMA 缓冲和控制资源所有权。
  失败后不自动 retry/reset，也不开放新采集；成功会话可停止/重新启动，租约与守卫门禁保留。
- 相对 v205 生产代码仅修改 `a733_camera_live.c`、`a733_camera_control.c`、`a733_camera_stream.inc`、`a733_csi_capture.c`。
  NPU/语音全部源文件不变；原 NPU 硬件执行、内存实现及公开 ABI 仍保持通过版边界。
- ELF 24 构造项，永久 4 MiB 帧缓冲，运行区结束 `0x45dda000`；无新的共享 IOMMU/PMIC 写权限。
- 内核 7111272 字节；SHA256 `793803d6c3285b13167b83acf9bf546ae4d96055d7c30efa1c21e6d2079ce201`；U-Boot CRC32 `6e71a03f`。
- 镜像 `openvela-a733-cubie-a7z-sd-v206-camera-stream-repair-candidate.img`，2147483648 字节；SHA256
  `1a11333831fec58a247e3d61a85eb7cbf1350347dae1903432536a0f6f0746eb`，WSL 与 Windows 独立全文件核对一致。
- ext4/e2fsck、内核/脚本回读、GPT/基线未修改分区哈希通过；原 FAT/模型/引导固件不改。只生成普通文件，未写物理卡/UFS。
- v204 镜像 SHA `f21a9d0bd8aab20264db3e44273ebf372c629f33ee7bba6c49659f0e2e82326d` 和 v205 SHA
  `826fe505c026ffa0c75fc7882008c831f1c33560f6649ba856b07770a16c89a7` 已重新核对，原标签与恢复记录不变。

`summary.json` 绑定各项日志/报告、固件源文件与 10 项外部封存文件哈希，不包含私人照片或模型二进制。
原生 ARM 工具仍沿用 v205 测试协议标识，性能细分行仍叫 `live205`；实际固件状态头必须出现 `camera-live-v206` / `stream-v206`。
测试顺序及已知限制：[v206 板测文档](../../a733/IMX214_V206_STREAM_REPAIR_TEST.md)。先 10 秒预览，再 30 秒 YOLO；失败立即保存现场，不重复重试。

本轮第一次 host bundle 在 `out/perf-check-v206-r1` 失败，是新排空失败夹具在 rearm 前而非帧完成后注入，
驱动按预期在重启之前拒绝；修正夹具时机后，r2 与最终验收中的全组通过。原失败日志保留，不当作固件通过证据。
