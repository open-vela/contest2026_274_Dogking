# v217-audit2 审计证据

固件：`8c5a7b3b267128b7859c37d1e1000ec9054732f4`。
本目录记录**软件及镜像检查，不是整机／云 API／声学验收**。

- `summary.json`：当前推荐审计镜像、摘要、源码、封存／恢复目录及未验收项。
- `host-arm-summary.json`：本轮新执行的 19 项语音回归＋4 项产品门禁；实际 runtime 的所有权／取消／SIGINT／私有默认策略测试含在 product-core 门禁中。
- `full-link-summary.json`：目标构建、源码绑定、受保护执行函数／ABI、相机、快照、ELF／ISA。
- `package-summary.json`：实际镜像中的启动／文件系统／模型回读及最终 SHA256。
- `independent-windows-hashes.json`：Windows 独立读取新镜像并核对旧 v1.0／v216／v204 摘要。

真实产品服务测试使用模拟模型／PCM；MCU 使用真实编码器与 PTY，不运行物理电机；相机模拟设备不算出图。
旧 v1.0 与本次源码／制品分别保留，不覆盖旧版证据。
本轮临时 SDK 修改的 309 个路径逐字节恢复。

详见 [审计报告](../../a733/PRODUCT_V1_0_AUDIT.md)、[当前技术总览](../../PROJECT_FULL_SUMMARY.md)、[说明书](../../PROJECT_MANUAL_V1_0.md)、[初赛后变化](../../POST_PRELIMINARY_IMPROVEMENTS.md)。
