# v1.0 / v217 软件发布证据

封存日期：2026-10-31。**不是新板测、声学验收、云 API 验收或延迟达标证明。**

- `summary.json`：最终镜像/固件摘要和显式的验收边界。
- `host-arm-summary.json`：19 项 v216 回归，加产品规则、真实服务的模拟后端测试、真实 UART 协议/PTY 故障测试、9 个实际 NuttX ARM 编译单元，共 23 项门禁。
- `full-link-summary.json`：完整摄像头回归、公开 ABI/受保护 NPU 执行源、ELF/ISA、24 项构造器、保留区边界和产品功能实际链接。
- `package-summary.json`：启动脚本、EXT4/GPT、49 项私有语音资产逐项回读，以及 MiniCPM5/YOLO/WAV 校验。

产品模式下的相机快照测试实际编译相机协调器：保留语音时拒绝启动、每请求只发布一帧、唯一 token、旧/取消请求不复用结果、CRC/IRQ 错误与清理失败隔离。
设备和引擎为模拟实现，不冒充真正摄像头/NPU/ASR/TTS 执行。
STM32 参考源码固定于 `e83a9cac8a0e4a775a38910bdaf08c68bfdffc8c`；与其 C 编码器对照 603 帧，并验证迟到 ACK、先到故障、NAK 和超时不能恢复运动权限。

最终固件源码为 `77c5c6fe91cdcc1f5a021063b2b0c7788e736e94`，实际构建为 `out/product-v1-build-r4`。
`CONFIG_A733_PRODUCT_V1=y` 已检查，临时暂存 SDK 的 309 个路径逐字节恢复。
内核 7205664 字节，运行区末端 `0x45df1000`，低于 `0x48000000` 保留边界。
MiniCPM5 的真实 GGUF 参考证据沿用 `out/llm-quality-v216-r4/summary.json`，三个数值执行源与该次源码保持相同；本轮不把路由模拟测试当作重新执行过真实模型。

完整接线/验收：[产品指南](../../a733/PRODUCT_V1_0.md)。
镜像摘要/回退/私有权重许可：[发布与恢复](../../a733/PRODUCT_V1_0_RELEASE.md)。
