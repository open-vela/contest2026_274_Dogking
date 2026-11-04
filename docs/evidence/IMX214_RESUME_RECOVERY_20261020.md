# IMX214 后续推进与恢复准备

日期：2026-10-20。用户授权语音阶段后继续完整 MIPI→YOLO→SPI 链路，并暂缓新旧
板测统一执行，要求所有阶段可恢复、可继续改进。实施主约定见
[路线与板测账本](../a733/VOICE_CAMERA_RECOVERY_ROADMAP.md)。

## 本轮完成的范围

- 保留 v162 音频通过镜像、v163 候选/源码/capsule 原样；未改任何运行时驱动、
  模型、语音 gain、应用行为或官方 SDK，没有生成冒充已完成摄像头的新镜像。
- 核对官方 A7Z 13M214 的现行参考流程，调整模式选择依据；完成本地资料入口核对，
  没有复制 GPL Linux 驱动/表到 Apache BSP，也没有写入未核对的 PMIC/CCI/CSI 寄存器。
- 新增只操作本地新目录的 checkpoint 工具，保留 source/docs、Git refs/HEAD 可达历史
  bundle、index/working-tree patch、未跟踪源码、删除记录及外部恢复文件哈希。
- 单元回归通过：暂存/未暂存内容不同也能恢复，原 Git/工作树不变；历史文档与未跟踪
  文件保留；已有快照/恢复目录拒绝，非法路径、缺 COMPLETE、归档/辅助文件损坏、
  外部引用变更均拒绝或明确返回校验边界。测试只使用临时目录，没有操作实机。
- 本轮推荐检查点使用 `out/checkpoints/a733-pre-imx214-20261020-r2`；完整工程的实际创建、
  逐文件恢复冒烟与引用校验结果以该目录 manifest/COMPLETE 和 out 中运行记录为准。
  恢复仅写新的 `out/recovery-a733-pre-imx214-smoke-r2-20261020`，不 reset/checkout 原项目。

## Windows 完整恢复发现的问题

首版 864 文件/9 引用检查点创建与内容校验成功，但真实项目恢复至深目录时，
`kaldi-native-fbank/PROVENANCE.json` 的目标相对路径共 261 字符，Windows 返回文件不存在。
首次恢复为失败，不是成功；原项目、首版检查点和部分恢复目录全部保留。
仅改为普通绝对路径仍不足，新增 >260 字符的专门回归首先复现了 WinError3；
最终文件 IO 使用 Windows 扩展绝对路径，Git 命令使用 core.longpaths，三组回归通过。
没有修改 Windows 注册表、系统策略、原 Git 配置或截短工程文件名来绕过问题。
r2 是为此修复新建的检查点，不覆盖首版证据；未改 v163 内核/镜像。

## 参考来源与关键风险

官方主来源：[A7Z / Radxa Camera 13M214](https://docs.radxa.com/en/cubie/a7z/accessories/camera-13m-214)。
按该页先以既有 1080p 流程核对模式；它是 Linux 经验，不能推导 openvela 的 ISP 已可用。
本地文件均只读，SHA-256 固定如下（从本项目根目录的上级查找）：

- `bsp-product-aiot-stable/drivers/vin/modules/sensor/imx214.c`
  `e05a71fe4843c81d354ab9fa30bbd809796c21d557a87af54b550ba4e4a23832`。
- `linux-5.15-product-aiot-stable/drivers/media/i2c/imx214.c`
  `92cae9c23d8b02cc7317f873c9a5a1c5c986de329e8b963b74721d43d57be328`。
- `allwinner-device-device-a733-v1.4.6/configs/cubie_a7z/linux-6.6/board.dts`
  `05fc6ebd0dff3c10ea4a7d820e6d1712a48059a447f2e9108d4f392e6bb57380`。

旧全志 `sensor_detect` 读取两个字节后无条件成功，没有错误与 ID 比较；板级默认
sensor0 仍写 OV13850。不能照搬它们作为 IMX214 接线/身份已验证的证据。
ISP 运行依赖、IQ 数据、实际启用 overlay 与 AF 等仍需继续核对。

## 不在本轮宣称的结果

未启动摄像头、未采集图像/语音、未 SSH、未联网板测、未刷卡、未推送远端。
没有 IMX214 sensor/CSI/VIN/ISP 实机通过；“文件 YOLO 框层通过”不等于摄像头通过。
v163 WAV 数值差异和完整语音 ASR 仍是原待办，不因后续视觉规划而自动完成。
恢复冒烟只验证文件/Git 状态，不是冷启动、硬件长稳或 SD `/data` 灾备。
